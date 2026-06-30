#ifndef _MSM_XARRAY_BACKPORT_H_
#define _MSM_XARRAY_BACKPORT_H_

#include <linux/version.h>

/*
 * TODO(version-boundary): The real XArray data structure was merged in 4.20
 * (commit f6bb2a2c0b81). On 4.19 <linux/xarray.h> is a "ghost" header: it only
 * provides the xa_lock() spinlock macros, NOT struct xarray / xa_alloc /
 * xa_for_each. So for < 4.20 we emulate the slice of the XArray API that the
 * backported DRM scheduler needs on top of IDR. On >= 4.20 the in-tree header
 * is authoritative and this whole shim collapses to nothing.
 */
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 20, 0)

#include <linux/idr.h>
#include <linux/slab.h>

// 1. Redefine the structure to use IDR under the hood
struct xarray {
    struct idr idr;
};

#define XA_FLAGS_ALLOC 0
#define xa_limit_32b 0, UINT_MAX

// 2. Lifecycle mappings
static inline void xa_init_flags(struct xarray *xa, gfp_t flags)
{
    idr_init(&xa->idr);
}

static inline void xa_destroy(struct xarray *xa)
{
    idr_destroy(&xa->idr);
}

// 3. State and Modification queries
static inline bool xa_empty(struct xarray *xa)
{
    return idr_is_empty(&xa->idr);
}

static inline int xa_alloc(struct xarray *xa, uint32_t *id, void *entry, 
                           int min, int max, gfp_t gfp)
{
    // idr_alloc takes int*, so we pass it safely
    int allocated_id;
    int ret = idr_alloc(&xa->idr, entry, min, max, gfp);
    if (ret >= 0) {
        *id = (uint32_t)ret;
        return 0;
    }
    return ret;
}

static inline void *xa_store(struct xarray *xa, unsigned long index, void *entry, gfp_t gfp)
{
    void *old = idr_find(&xa->idr, index);
    if (old) {
        idr_replace(&xa->idr, entry, index);
    } else {
        // If it doesn't exist, we force-allocate at that exact index
        idr_alloc(&xa->idr, entry, index, index + 1, gfp);
    }
    return old;
}

static inline void *xa_erase(struct xarray *xa, unsigned long index)
{
    void *old = idr_find(&xa->idr, index);
    if (old)
        idr_remove(&xa->idr, index);
    return old;
}

// 4. Iterator macro mapping
//
// idr_get_next() takes an `int *nextid`, but the upstream scheduler code
// iterates with an `unsigned long index`. We can't use idr_for_each_entry()
// directly (incompatible-pointer-types -Werror). Cast the address instead:
// this is safe on little-endian (ARM64) because every id we ever store is
// xa_limit_32b, so it lives entirely in the low 32 bits and `index` is zeroed
// at loop entry (high word stays clear). gnu89-safe: declares nothing inline.
#define xa_for_each(xa, index, entry) \
    for ((index) = 0; \
         ((entry) = idr_get_next(&(xa)->idr, (int *)&(index))) != NULL; \
         (index)++)

#else  /* >= 4.20: use the real <linux/xarray.h> */

#include <linux/xarray.h>

#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(4, 20, 0) */

#endif /* _MSM_XARRAY_BACKPORT_H_ */
