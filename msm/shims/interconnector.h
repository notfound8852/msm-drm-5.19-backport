#ifndef INTERCONNECTOR_H
#define INTERCONNECTOR_H

/**
 *
 * Upstream/Mainline completely transitioned from the legacy msm-bus API to the
 * newly refractored interconnects subsystem introduced in >= Linux 5.1. This allows
 * for clean parsing. "interconnects" property and "interconnect-names"
 * (e.g., "mdp0-mem") are parsed dynamically from the Device Tree via of_icc_get()
 * This then allows the driver to scale each an every path individually allowing for
 * both synchronous and asynchronous scalling.
 *
 * PROBLEM: Downstream < 5.1 kernels completely lacks this framework relying on
 * verbose DTB multi-vector arrays and hardcoded "expected" bandwidth levels in
 * order to satisfy msm_bus_scale_client_update_request(). which then generates a
 * client handle before finally allowing you to select a "case" from the hardcoded
 * cases. The obvious issue is; dpu_core_perf.c calculates new B/W rates at runtime...
 * We can't predict what case it's going to want and when.
 *
 * NOTE: There does exist a kernel API for downstream that allows for the same kind of
 * control going all the way back to the days msm-bus was first added around Linux 3.x.
 * It allows for both synchronous and asynchronous scalling on individual paths. The
 * only problem is that it expects us to pass the "src" and "dst" when invoking it. In
 * other words, it's both extra labor and would require us to hardcode these "magic"
 * msm-bus specific numbers(again) which destroys any potential for multi-SoC portability
 * as I am sure msm-bus values can differ across KVERSIONS... .~.
 *
 * SOLUTION: We can manually parse the DTBs in our shim function of_icc_get() and
 * extract the <src dst> tuple directly from a modern looking "interconnects"
 * property.
 *
 * This then allows performance logic (like dpu_core_perf.c) to stream raw floor/
 * ceiling bandwidth allocations directly through icc_set_bw() which is just full-on
 * a wrapper for msm_bus_scale_update_bw(), keeping the memory bus saturated enough
 * to prevent catastrophic panel underflows.
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
 * @paths: Array of allocated icc_path pointers
 * @num_paths: Total number of active paths in the array
 */
struct devm_icc_closure {
    struct icc_path **paths;
    u32 num_paths;
};

/**
 * Modern interconnect drivers handle absolute bytes/sec values natively.
 * These helpers translate throughput directly to the raw values expected
 * by the underlying downstream scaling engine.
 */
#define KBps_to_icc(x) ((u64)(x) * 1000ULL)
#define MBps_to_icc(x) ((u64)(x) * 1000ULL * 1000ULL)
#define Bps_to_icc(x) (u64)(x)

int of_icc_get(struct device *dev,
                 struct icc_path **paths,
                 u32 *num_paths);

int icc_set_bw(struct icc_path *path, u64 ab, u64 ib);

void icc_put(struct icc_path *path);


/**
 * NOTE: This function isn't used in the MSM driver itself but instead in
 * the OPP shim where this function becomes extremely important to prevent
 * memory leaks.
 */
int devm_of_icc_get(struct device *dev,
                      struct icc_path **paths,
                      u32 *num_paths);

#endif /* INTERCONNECTOR_H */
