#ifndef INTERCONNECTOR_H
#define INTERCONNECTOR_H

/*
 * Interconnect shim - the mainline icc_* API the MSM driver calls, backed by
 * downstream msm-bus.
 *
 * mainline never had msm-bus; it had no bandwidth abstraction at all until the
 * interconnect framework arrived in 5.1. So a 5.19 driver on a 4.19 CAF base is
 * calling an API the platform does not have, against a platform API the driver
 * has never heard of. This bridges the two: icc_set_bw() is msm_bus_scale_
 * update_bw(), and paths come from an "interconnects" property.
 *
 * That property is NOT the mainline binding. Mainline uses
 * <&provider MASTER &provider SLAVE>; there is no provider node here, so this
 * shim expects two bare cells per path holding the msm-bus src and dst IDs
 * directly. "interconnect-names" is not supported - lookup is by index.
 *
 * See Documentation/core/interconnector.md.
 */

#include <linux/msm-bus.h>
#include <linux/of.h>

/**
 * struct icc_path - Shim handle representing an interconnect path
 * @handle: Pointer to the underlying legacy Qualcomm bus client handle
 */
struct icc_path {
    struct msm_bus_client_handle *handle;
};

/**
 * struct devm_icc_closure - Tracking structure for resource-managed paths
 * @num_paths: Total number of active paths in the array
 * @paths: Flexible array holding a private copy of the allocated icc_path
 *         pointers, so the closure does not depend on the caller's array
 *         outliving it
 */
struct devm_icc_closure {
    u32 num_paths;
    struct icc_path *paths[];
};

/*
 * Bandwidth is in bytes/sec, as mainline. Same macros.
 */
#define MBps_to_icc(x) ((u64)(x) * 1000ULL * 1000ULL)
#define KBps_to_icc(x) ((u64)(x) * 1000ULL)
#define Bps_to_icc(x) (u64)(x)

/**
 * of_icc_get() - Acquire every path named in the device's "interconnects"
 * @dev: Device whose node carries the property
 * @paths: Caller-provided array, filled in device tree order
 * @num_paths: Out, number of paths acquired; set to 0 on failure
 *
 * CALLER MUST SIZE @paths. This writes one entry per <src dst> pair in the
 * property without knowing the array's capacity - derive the count from the
 * same property first, or the write runs off the end.
 *
 * On failure every acquired path is released and @paths is cleared, so the
 * array is safe to hand to a teardown path either way.
 *
 * Return: 0 on success. -ENODEV if the property is absent, -EINVAL if its
 * length is not a whole number of <src dst> pairs.
 */
int of_icc_get(struct device *dev,
                 struct icc_path **paths,
                 u32 *num_paths);

/**
 * icc_set_bw() - Vote bandwidth on a path
 * @path: Path to vote on
 * @ab: Average bandwidth, bytes/sec
 * @ib: Peak bandwidth, bytes/sec
 *
 * Both zero removes the vote.
 *
 * Return: 0 on success, negative errno otherwise.
 */
int icc_set_bw(struct icc_path *path, u64 ab, u64 ib);

/**
 * icc_put() - Drop a path's vote and release it
 * @path: Path to release, may be NULL
 */
void icc_put(struct icc_path *path);

/**
 * devm_of_icc_get() - Managed of_icc_get()
 * @dev: Device whose node carries the property
 * @paths: Caller-provided array, filled in device tree order
 * @num_paths: Out, number of paths acquired; set to 0 on failure
 *
 * Same contract as of_icc_get(), plus release at unbind. For probe paths with
 * nowhere else to keep the handles; a caller that already owns a devres node
 * should acquire into that instead, so the handles have one owner rather than
 * two records.
 *
 * Return: as of_icc_get(), or -ENOMEM.
 */
int devm_of_icc_get(struct device *dev,
                      struct icc_path **paths,
                      u32 *num_paths);

#endif /* INTERCONNECTOR_H */
