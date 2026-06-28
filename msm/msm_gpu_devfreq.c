// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2013 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 */

#include "msm_gpu.h"
#include "msm_gpu_trace.h"

#include <linux/devfreq.h>
#include <linux/devfreq_cooling.h>

#if LINUX_VERSION_CODE > KERNEL_VERSION(5, 16, 0)
#include <linux/math64.h>
#include <linux/units.h>
#endif

/*
 * Power Management:
 */

#if LINUX_VERSION_CODE > KERNEL_VERSION(5, 16, 0)
static void get_raw_dev_status(struct msm_gpu *gpu,
		struct devfreq_dev_status *status)
{
	struct msm_gpu_devfreq *df = &gpu->devfreq;
	u64 busy_cycles, busy_time;
	unsigned long sample_rate;
	ktime_t time;

	status->current_frequency = gpu->funcs->gpu_get_freq ? gpu->funcs->gpu_get_freq(gpu) : clk_get_rate(gpu->core_clk);
	busy_cycles = gpu->funcs->gpu_busy(gpu, &sample_rate);
	time = ktime_get();

	busy_time = busy_cycles - df->busy_cycles;
	status->total_time = ktime_us_delta(time, df->time);

	df->busy_cycles = busy_cycles;
	df->time = time;

	busy_time *= USEC_PER_SEC;
	do_div(busy_time, sample_rate);
	if (WARN_ON(busy_time > ~0LU))
			busy_time = ~0LU;

	status->busy_time = busy_time;
}

static void update_average_dev_status(struct msm_gpu *gpu,
		const struct devfreq_dev_status *raw)
{
	struct msm_gpu_devfreq *df = &gpu->devfreq;
	const u32 polling_ms = df->devfreq->profile->polling_ms;
	const u32 max_history_ms = polling_ms * 11 / 10;
	struct devfreq_dev_status *avg = &df->average_status;
	u64 avg_freq;

	if (div_u64(raw->total_time, USEC_PER_MSEC) >= polling_ms || !avg->total_time) {
			*avg = *raw;
			return;
	}

	if (div_u64(avg->total_time + raw->total_time, USEC_PER_MSEC) >= max_history_ms) {
			const u32 new_total_time = polling_ms * USEC_PER_MSEC - raw->total_time;
			avg->busy_time = div_u64(mul_u32_u32(avg->busy_time, new_total_time), avg->total_time);
			avg->total_time = new_total_time;
	}

	avg_freq = mul_u32_u32(avg->current_frequency, avg->total_time);
	avg_freq += mul_u32_u32(raw->current_frequency, raw->total_time);
	do_div(avg_freq, avg->total_time + raw->total_time);

	avg->current_frequency = avg_freq;
	avg->busy_time += raw->busy_time;
	avg->total_time += raw->total_time;
}
#endif

static unsigned long get_freq(struct msm_gpu *gpu)
{
#if LINUX_VERSION_CODE <= KERNEL_VERSION(5, 16, 0)
	if (gpu->devfreq.idle_freq)
			return gpu->devfreq.idle_freq;
#endif

	if (gpu->funcs->gpu_get_freq)
			return gpu->funcs->gpu_get_freq(gpu);

	return clk_get_rate(gpu->core_clk);
}

static int msm_devfreq_target(struct device *dev, unsigned long *freq, u32 flags)
{
	struct msm_gpu *gpu = dev_to_gpu(dev);
	struct dev_pm_opp *opp;

	opp = devfreq_recommended_opp(dev, freq, flags);

#if LINUX_VERSION_CODE <= KERNEL_VERSION(5, 16, 0)
	if (gpu->devfreq.idle_freq) {
			gpu->devfreq.idle_freq = *freq;
			dev_pm_opp_put(opp);
			return 0;
	}
#endif

	if (IS_ERR(opp))
			return PTR_ERR(opp);

	trace_msm_gpu_freq_change(dev_pm_opp_get_freq(opp));

	if (gpu->funcs->gpu_set_freq)
			gpu->funcs->gpu_set_freq(gpu, opp);
	else
			clk_set_rate(gpu->core_clk, *freq);

	dev_pm_opp_put(opp);
	return 0;
}
/*
 * The reason why the Legacy path works is because it calculates everything itself.
*/
static int msm_devfreq_get_dev_status(struct device *dev, struct devfreq_dev_status *status)
{
	struct msm_gpu *gpu = dev_to_gpu(dev);
	ktime_t time;

#if LINUX_VERSION_CODE > KERNEL_VERSION(5, 16, 0)
	struct devfreq_dev_status raw;
	get_raw_dev_status(gpu, &raw);
	update_average_dev_status(gpu, &raw);
	*status = gpu->devfreq.average_status;
#else
	unsigned long sample_rate = 0; /* Dummy to satisfy 5.19 signature */

	status->current_frequency = get_freq(gpu);
	/* Pass the pointer to satisfy the 5.19 driver's expectation */
	status->busy_time = gpu->funcs->gpu_busy(gpu, &sample_rate);

	time = ktime_get();
	status->total_time = ktime_us_delta(time, gpu->devfreq.time);
	gpu->devfreq.time = time;
#endif

	return 0;
}

static int msm_devfreq_get_cur_freq(struct device *dev, unsigned long *freq)
{
	*freq = get_freq(dev_to_gpu(dev));
	return 0;
}

static struct devfreq_dev_profile msm_devfreq_profile = {
#if LINUX_VERSION_CODE > KERNEL_VERSION(5, 14, 0)
	.timer = DEVFREQ_TIMER_DELAYED,
#endif
	.polling_ms = 50,
	.target = msm_devfreq_target,
	.get_dev_status = msm_devfreq_get_dev_status,
	.get_cur_freq = msm_devfreq_get_cur_freq,
};

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 16, 0)
static void msm_devfreq_boost_work(struct kthread_work *work);
#endif
static void msm_devfreq_idle_work(struct kthread_work *work);

static bool has_devfreq(struct msm_gpu *gpu)
{
	struct msm_gpu_devfreq *df = &gpu->devfreq;
	return !!df->devfreq;
}

static void cancel_idle_work(struct msm_gpu_devfreq *df)
{
	hrtimer_cancel(&df->idle_work.timer);
#if LINUX_VERSION_CODE > KERNEL_VERSION(5, 16, 0)
	kthread_cancel_work_sync(&df->idle_work.work);
#endif
}

#if LINUX_VERSION_CODE > KERNEL_VERSION(5, 16, 0)
static void cancel_boost_work(struct msm_gpu_devfreq *df)
{
	hrtimer_cancel(&df->boost_work.timer);
	kthread_cancel_work_sync(&df->boost_work.work);
}
#endif

void msm_devfreq_init(struct msm_gpu *gpu)
{
	struct msm_gpu_devfreq *df = &gpu->devfreq;

	if (!gpu->funcs->gpu_busy)
			return;

#if LINUX_VERSION_CODE <= KERNEL_VERSION(5, 16, 0)
	pr_info("Frequency QoS not supported on legacy code, using direct target mode\n");
#else
	dev_pm_qos_add_request(&gpu->pdev->dev, &df->idle_freq,
						   DEV_PM_QOS_MAX_FREQUENCY,
						   PM_QOS_MAX_FREQUENCY_DEFAULT_VALUE);
	dev_pm_qos_add_request(&gpu->pdev->dev, &df->boost_freq,
						   DEV_PM_QOS_MIN_FREQUENCY, 0);
#endif

	msm_devfreq_profile.initial_freq = gpu->fast_rate;
	msm_devfreq_profile.freq_table = NULL;
	msm_devfreq_profile.max_state = 0;

	df->devfreq = devm_devfreq_add_device(&gpu->pdev->dev,
					&msm_devfreq_profile, DEVFREQ_GOV_SIMPLE_ONDEMAND,
					NULL);

	if (IS_ERR(df->devfreq)) {
			DRM_DEV_ERROR(&gpu->pdev->dev, "Couldn't initialize GPU devfreq\n");
			df->devfreq = NULL;
			return;
	}

	devfreq_suspend_device(df->devfreq);

	gpu->cooling = of_devfreq_cooling_register(gpu->pdev->dev.of_node, df->devfreq);
	if (IS_ERR(gpu->cooling)) {
			DRM_DEV_ERROR(&gpu->pdev->dev, "Couldn't register GPU cooling device\n");
			gpu->cooling = NULL;
	}

#if LINUX_VERSION_CODE > KERNEL_VERSION(5, 16, 0)
	msm_hrtimer_work_init(&df->boost_work, gpu->worker, msm_devfreq_boost_work,
						  CLOCK_MONOTONIC, HRTIMER_MODE_REL);
#endif
	msm_hrtimer_work_init(&df->idle_work, gpu->worker, msm_devfreq_idle_work,
						  CLOCK_MONOTONIC, HRTIMER_MODE_REL);
}

void msm_devfreq_cleanup(struct msm_gpu *gpu)
{
	struct msm_gpu_devfreq *df = &gpu->devfreq;

	if (!has_devfreq(gpu))
			return;

	devfreq_cooling_unregister(gpu->cooling);

#if LINUX_VERSION_CODE > KERNEL_VERSION(5, 16, 0)
	dev_pm_qos_remove_request(&df->boost_freq);
	dev_pm_qos_remove_request(&df->idle_freq);
#endif
}

void msm_devfreq_resume(struct msm_gpu *gpu)
{
	if (!has_devfreq(gpu))
			return;

	gpu->devfreq.busy_cycles = 0;
	gpu->devfreq.time = ktime_get();

	devfreq_resume_device(gpu->devfreq.devfreq);
}

void msm_devfreq_suspend(struct msm_gpu *gpu)
{
	struct msm_gpu_devfreq *df = &gpu->devfreq;

	if (!has_devfreq(gpu))
			return;

	devfreq_suspend_device(df->devfreq);

	cancel_idle_work(df);
#if LINUX_VERSION_CODE > KERNEL_VERSION(5, 16, 0)
	cancel_boost_work(df);
#endif
}

#if LINUX_VERSION_CODE > KERNEL_VERSION(5, 16, 0)
static void msm_devfreq_boost_work(struct kthread_work *work)
{
	struct msm_gpu_devfreq *df = container_of(work, struct msm_gpu_devfreq, boost_work.work);
	dev_pm_qos_update_request(&df->boost_freq, 0);
}

void msm_devfreq_boost(struct msm_gpu *gpu, unsigned factor)
{
	struct msm_gpu_devfreq *df = &gpu->devfreq;
	uint64_t freq;

	if (!has_devfreq(gpu))
			return;

	freq = get_freq(gpu);
	freq *= factor;

	do_div(freq, HZ_PER_KHZ);

	dev_pm_qos_update_request(&df->boost_freq, freq);

	msm_hrtimer_queue_work(&df->boost_work,
						   ms_to_ktime(msm_devfreq_profile.polling_ms),
						   HRTIMER_MODE_REL);
}
#endif

void msm_devfreq_active(struct msm_gpu *gpu)
{
	struct msm_gpu_devfreq *df = &gpu->devfreq;
	unsigned int idle_time;
#if LINUX_VERSION_CODE <= KERNEL_VERSION(5, 16, 0)
	struct devfreq_dev_status status;
	unsigned long target_freq = df->idle_freq;
#endif

	if (!has_devfreq(gpu))
			return;

	cancel_idle_work(df);

#if LINUX_VERSION_CODE <= KERNEL_VERSION(5, 16, 0)
	mutex_lock(&df->devfreq->lock);
	idle_time = ktime_to_ms(ktime_sub(ktime_get(), df->idle_time));

	if (idle_time > msm_devfreq_profile.polling_ms / 2) {
			target_freq *= 2;
	}

	df->idle_freq = 0;
	msm_devfreq_target(&gpu->pdev->dev, &target_freq, 0);
	msm_devfreq_get_dev_status(&gpu->pdev->dev, &status);
	mutex_unlock(&df->devfreq->lock);
#else
	idle_time = ktime_to_ms(ktime_sub(ktime_get(), df->idle_time));

	if (idle_time > msm_devfreq_profile.polling_ms) {
			msm_devfreq_boost(gpu, 2);
	}

	dev_pm_qos_update_request(&df->idle_freq, PM_QOS_MAX_FREQUENCY_DEFAULT_VALUE);
#endif
}

static void msm_devfreq_idle_work(struct kthread_work *work)
{
	struct msm_gpu_devfreq *df = container_of(work, struct msm_gpu_devfreq, idle_work.work);
	struct msm_gpu *gpu = container_of(df, struct msm_gpu, devfreq);

#if LINUX_VERSION_CODE <= KERNEL_VERSION(5, 16, 0)
	unsigned long idle_freq, target_freq = 0;

	mutex_lock(&df->devfreq->lock);
	idle_freq = get_freq(gpu);

	if (gpu->clamp_to_idle)
			msm_devfreq_target(&gpu->pdev->dev, &target_freq, 0);

	df->idle_time = ktime_get();
	df->idle_freq = idle_freq;
	mutex_unlock(&df->devfreq->lock);
#else
	df->idle_time = ktime_get();

	if (gpu->clamp_to_idle)
			dev_pm_qos_update_request(&df->idle_freq, 0);
#endif
}

void msm_devfreq_idle(struct msm_gpu *gpu)
{
	struct msm_gpu_devfreq *df = &gpu->devfreq;

	if (!has_devfreq(gpu))
			return;

	msm_hrtimer_queue_work(&df->idle_work, ms_to_ktime(1), HRTIMER_MODE_REL);
}
