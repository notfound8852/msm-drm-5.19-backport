#ifndef INTERCONNECTOR_H
#define INTERCONNECTOR_H

/*
 * Interconnect shim - the mainline icc_* API the MSM driver calls, backed by
 * downstream msm-bus.
 *
 * Mainline never had msm-bus; it had no bandwidth abstraction at all until the
 * interconnect framework arrived in 5.1. So a 5.19 driver on a 4.19 CAF base is
 * calling an API the platform does not have, against a platform API the driver
 * has never heard of. This bridges the two: icc_set_bw() is msm_bus_scale_
 * update_bw(), and paths come from an "interconnects" property.
 *
 * The signatures and the return contract are mainline's, so consumers need no
 * version gate - a call site can be lifted out of a mainline tree unchanged.
 *
 * The DT is where the two differ. Mainline writes
 * <&provider MASTER &provider SLAVE>; there is no provider node to phandle to
 * on 4.19, so this shim expects two bare cells per path carrying the msm-bus
 * src and dst IDs directly. "interconnect-names" keeps its mainline meaning and
 * is required for name-based lookup.
 *
 * See Documentation/core/interconnector.md.
 */
#include <linux/version.h>

#define HAS_BASIC_ICC		(LINUX_VERSION_CODE >= KERNEL_VERSION(5, 1, 0))
#define HAS_ADVANCE_ICC		(LINUX_VERSION_CODE >= KERNEL_VERSION(5, 8, 0))

#if !HAS_BASIC_ICC
#include <linux/msm-bus.h>
#include <linux/of.h>
/**
 * struct icc_path - Shim handle representing an interconnect path
 * @handle: Pointer to the underlying legacy Qualcomm bus client handle
 */
struct icc_path {
    struct msm_bus_client_handle *handle;
};

/*
 * Bandwidth is in bytes/sec, as mainline. Same macros.
 */
#define MBps_to_icc(x) ((u64)(x) * 1000ULL * 1000ULL)
#define KBps_to_icc(x) ((u64)(x) * 1000ULL)
#define Bps_to_icc(x) (u64)(x)

/**
 * of_icc_get() - Acquire a path by name from the device's "interconnects"
 * @dev: Consumer device
 * @name: Entry in "interconnect-names", or NULL for the first path
 *
 * Return: The path. NULL - not an error - if @dev declares no "interconnects",
 * which is what lets a consumer treat bandwidth voting as optional. ERR_PTR
 * otherwise: -ENODEV with no OF node, -EINVAL if "interconnects" is malformed,
 * and whatever of_property_match_string() returned if @name is not listed in
 * "interconnect-names".
 */
struct icc_path *of_icc_get(struct device *dev, const char *name);

/**
 * of_icc_get_by_index() - Acquire a path by position in "interconnects"
 * @dev: Consumer device
 * @idx: Zero-based index of the <src dst> pair
 *
 * Return: as of_icc_get(), plus -ENOENT if @idx is out of range.
 */
struct icc_path *of_icc_get_by_index(struct device *dev, int idx);
/**
 * of_icc_get_count() - Number of paths the device declares
 * @dev: Consumer device
 *
 * NOT a mainline call. Mainline consumers know their paths by name and leave
 * enumeration to the provider; there is no provider here, so a consumer that
 * wants every path - the OPP shim - has to count the cells itself.
 *
 * Return: The count, or 0 if "interconnects" is absent or malformed.
 */
int of_icc_get_count(struct device *dev);

/**
 * icc_set_bw() - Vote bandwidth on a path
 * @path: Path to vote on, may be NULL
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
#else
#include <linux/interconnect.h>
#endif /* !HAS_BASIC_ICC */

#if !HAS_ADVANCE_ICC
/**
 * devm_of_icc_get() - Managed of_icc_get()
 * @dev: Consumer device
 * @name: Entry in "interconnect-names", or NULL for the first path
 *
 * Return: as of_icc_get(). The path is released at unbind.
 */
struct icc_path *devm_of_icc_get(struct device *dev, const char *name);
#endif

#endif /* INTERCONNECTOR_H */
