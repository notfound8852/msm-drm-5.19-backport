// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2013 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 */

#include <linux/dma-buf.h>

#include <drm/drm_prime.h>

#include "msm_drv.h"
#include "msm_gem.h"
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 16, 0)

int msm_gem_prime_mmap(struct drm_gem_object *obj, struct vm_area_struct *vma)
{
	int ret;

	/* Ensure the mmap offset is initialized.  We lazily initialize it,
	 * so if it has not been first mmap'd directly as a GEM object, the
	 * mmap offset will not be already initialized.
	 */

	ret = drm_gem_create_mmap_offset(obj);
	if (ret)
		return ret;

	return drm_gem_prime_mmap(obj, vma);
}
#else
//older version
int msm_gem_prime_mmap(struct drm_gem_object *obj, struct vm_area_struct *vma)
{
    int ret;

    ret = drm_gem_mmap_obj(obj, obj->size, vma);
    if (ret < 0)
        return ret;

    return msm_gem_mmap_obj(vma->vm_private_data, vma);
}
#endif
struct sg_table *msm_gem_prime_get_sg_table(struct drm_gem_object *obj)
{
	struct msm_gem_object *msm_obj = to_msm_bo(obj);
	int npages = obj->size >> PAGE_SHIFT;

	if (WARN_ON(!msm_obj->pages))  /* should have already pinned! */
		return ERR_PTR(-ENOMEM);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 9, 0)
	return drm_prime_pages_to_sg(obj->dev, msm_obj->pages, npages);
#else
    return drm_prime_pages_to_sg(msm_obj->pages, npages);
#endif
}
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 16, 0)
int msm_gem_prime_vmap(struct drm_gem_object *obj, struct iosys_map *map)
{
	void *vaddr;

	vaddr = msm_gem_get_vaddr(obj);
	if (IS_ERR(vaddr))
		return PTR_ERR(vaddr);
	iosys_map_set_vaddr(map, vaddr);

	return 0;
}
#else
void *msm_gem_prime_vmap(struct drm_gem_object *obj)
{
    return msm_gem_get_vaddr(obj);
}
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 16, 0)
void msm_gem_prime_vunmap(struct drm_gem_object *obj, struct iosys_map *map)
{
	msm_gem_put_vaddr(obj);
}
#else
void msm_gem_prime_vunmap(struct drm_gem_object *obj, void *vaddr)
{
    msm_gem_put_vaddr(obj);
}
#endif

struct drm_gem_object *msm_gem_prime_import_sg_table(struct drm_device *dev,
		struct dma_buf_attachment *attach, struct sg_table *sg)
{
	return msm_gem_import(dev, attach->dmabuf, sg);
}

int msm_gem_prime_pin(struct drm_gem_object *obj)
{
	if (!obj->import_attach)
		msm_gem_get_pages(obj);
	return 0;
}

void msm_gem_prime_unpin(struct drm_gem_object *obj)
{
	if (!obj->import_attach)
		msm_gem_put_pages(obj);
}
