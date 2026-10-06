// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2013-2016 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 */

#include <linux/dma-fence.h>
#include <linux/hrtimer.h>

#include "msm_drv.h"
#include "msm_fence.h"
#include "msm_gpu.h"

static struct msm_gpu *fctx2gpu(struct msm_fence_context *fctx)
{
	struct msm_drm_private *priv = fctx->dev->dev_private;

	return priv->gpu;
}

static enum hrtimer_restart deadline_timer(struct hrtimer *timer)
{
	struct msm_fence_context *fctx = container_of(timer,
			struct msm_fence_context, deadline_timer);
	struct msm_gpu *gpu = fctx2gpu(fctx);

	if (gpu && gpu->worker)
		kthread_queue_work(gpu->worker, &fctx->deadline_work);

	return HRTIMER_NORESTART;
}

static void deadline_work(struct kthread_work *work)
{
	struct msm_fence_context *fctx = container_of(work,
	struct msm_fence_context, deadline_work);
	struct msm_gpu *gpu = fctx2gpu(fctx);
	unsigned long flags;
	bool completed;

	if (!gpu)
		return;

	spin_lock_irqsave(&fctx->spinlock, flags);
	completed = msm_fence_completed(fctx, fctx->next_deadline_fence);
	spin_unlock_irqrestore(&fctx->spinlock, flags);

	if (!completed)
		msm_devfreq_boost(gpu, 2);
}

struct msm_fence_context *
msm_fence_context_alloc(struct drm_device *dev, volatile uint32_t *fenceptr,
		const char *name)
{
	struct msm_fence_context *fctx;
	static int index = 0;

	fctx = kzalloc(sizeof(*fctx), GFP_KERNEL);
	if (!fctx)
		return ERR_PTR(-ENOMEM);

	fctx->dev = dev;
	strncpy(fctx->name, name, sizeof(fctx->name));
	fctx->context = dma_fence_context_alloc(1);
	fctx->index = index++;
	fctx->fenceptr = fenceptr;
	spin_lock_init(&fctx->spinlock);
	hrtimer_init(&fctx->deadline_timer, CLOCK_MONOTONIC, HRTIMER_MODE_ABS);
	fctx->deadline_timer.function = deadline_timer;
	kthread_init_work(&fctx->deadline_work, deadline_work);
	fctx->next_deadline = ktime_get();

	return fctx;
}

void msm_fence_context_free(struct msm_fence_context *fctx)
{
	hrtimer_cancel(&fctx->deadline_timer);
	kthread_cancel_work_sync(&fctx->deadline_work);
	kfree(fctx);
}

bool msm_fence_completed(struct msm_fence_context *fctx, uint32_t fence)
{
	uint32_t curr_fence = READ_ONCE(*fctx->fenceptr);

	/*
	 * Note: Check completed_fence first, as fenceptr is in a write-combine
	 * mapping, so it will be more expensive to read.
	 */
	return (int32_t)(fctx->completed_fence - fence) >= 0 ||
	       (int32_t)(curr_fence - fence) >= 0;
}

/* called from irq handler and workqueue (in recover path) */
void msm_update_fence(struct msm_fence_context *fctx, uint32_t fence)
{
	unsigned long flags;

	spin_lock_irqsave(&fctx->spinlock, flags);
	if (fence_after(fence, fctx->completed_fence))
		fctx->completed_fence = fence;
	if (msm_fence_completed(fctx, fctx->next_deadline_fence))
		hrtimer_cancel(&fctx->deadline_timer);
	spin_unlock_irqrestore(&fctx->spinlock, flags);
}

struct msm_fence {
	struct dma_fence base;
	struct msm_fence_context *fctx;
};

static inline struct msm_fence *to_msm_fence(struct dma_fence *fence)
{
	return container_of(fence, struct msm_fence, base);
}

static const char *msm_fence_get_driver_name(struct dma_fence *fence)
{
	return "msm";
}

static const char *msm_fence_get_timeline_name(struct dma_fence *fence)
{
	struct msm_fence *f = to_msm_fence(fence);
	return f->fctx->name;
}

static bool msm_fence_signaled(struct dma_fence *fence)
{
	struct msm_fence *f = to_msm_fence(fence);
	return msm_fence_completed(f->fctx, f->base.seqno);
}

static void msm_fence_set_deadline_cb(struct dma_fence *fence,
				      ktime_t deadline)
{
	struct msm_fence *f = to_msm_fence(fence);
	struct msm_fence_context *fctx = f->fctx;
	unsigned long flags;
	ktime_t now;

	spin_lock_irqsave(&fctx->spinlock, flags);
	now = ktime_get();
	if (ktime_after(now, fctx->next_deadline) ||
	    ktime_before(deadline, fctx->next_deadline)) {
		struct msm_gpu *gpu = fctx2gpu(fctx);

		fctx->next_deadline = deadline;
		if (fence_after(fence->seqno, fctx->next_deadline_fence))
			fctx->next_deadline_fence = fence->seqno;

		deadline = ktime_sub(deadline, ms_to_ktime(3));
		if (ktime_after(now, deadline)) {
			if (gpu && gpu->worker)
				kthread_queue_work(gpu->worker,
						   &fctx->deadline_work);
		} else {
			hrtimer_start(&fctx->deadline_timer, deadline,
				      HRTIMER_MODE_ABS);
		}
	}
	spin_unlock_irqrestore(&fctx->spinlock, flags);
}

static const struct dma_fence_ops msm_fence_ops = {
	.get_driver_name = msm_fence_get_driver_name,
	.get_timeline_name = msm_fence_get_timeline_name,
	.signaled = msm_fence_signaled,
};

void msm_fence_set_deadline(struct dma_fence *fence, ktime_t deadline)
{
	if (fence && fence->ops == &msm_fence_ops && !dma_fence_is_signaled(fence))
		msm_fence_set_deadline_cb(fence, deadline);
}

struct dma_fence *
msm_fence_alloc(struct msm_fence_context *fctx)
{
	struct msm_fence *f;

	f = kzalloc(sizeof(*f), GFP_KERNEL);
	if (!f)
		return ERR_PTR(-ENOMEM);

	f->fctx = fctx;

	dma_fence_init(&f->base, &msm_fence_ops, &fctx->spinlock,
		       fctx->context, ++fctx->last_fence);

	return &f->base;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0)
void msm_resv_set_deadline(struct reservation_object *obj,
#else
void msm_resv_set_deadline(struct dma_resv *obj,
#endif
			   bool write, ktime_t deadline)
{
	struct dma_fence *excl = NULL;
	struct dma_fence **shared = NULL;
	unsigned int shared_count = 0;
	unsigned int i;
	int ret;

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0)
	ret = reservation_object_get_fences_rcu(obj, &excl, &shared_count, &shared);
#else
	ret = dma_resv_get_fences_rcu(obj, &excl, &shared_count, &shared);
#endif
	if (ret)
		return;

	/* Reads wait on writers; writes wait on both exclusive and shared fences. */
	msm_fence_set_deadline(excl, deadline);

	if (write && shared) {
		for (i = 0; i < shared_count; i++)
			msm_fence_set_deadline(shared[i], deadline);
	}

	if (excl)
		dma_fence_put(excl);

	if (shared) {
		for (i = 0; i < shared_count; i++)
			dma_fence_put(shared[i]);
		kfree(shared);
	}
}
