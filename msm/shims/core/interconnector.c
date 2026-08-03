#include "linux/interconnector.h"
#include <linux/slab.h>
#include <linux/export.h>

int of_icc_get(struct device *dev,
	       struct icc_path **paths,
	       u32 *num_paths)
{
	struct device_node *node = dev->of_node;
	const __be32 *data;
	int len, count, i;
	int ret;

	data = of_get_property(node, "interconnects", &len);
	if (!data)
		return -ENODEV;

	/* Each entry = <src dst> */
	if (len <= 0 || len % (sizeof(u32) * 2))
		return -EINVAL;

	count = len / (sizeof(u32) * 2);

	for (i = 0; i < count; i++) {
		u32 src = be32_to_cpu(data[i * 2]);
		u32 dst = be32_to_cpu(data[i * 2 + 1]);

		paths[i] = kzalloc(sizeof(struct icc_path), GFP_KERNEL);
		if (!paths[i]) {
			ret = -ENOMEM;
			goto err_rollback;
		}

		/*
		 * active_only = true.
		 *
		 * Mainline icc consumers (GPU/display) apply a vote while the
		 * device is powered and drop it on suspend; they never expect a
		 * persistent "sleep set" vote. Registering dual-context
		 * (active_only=false) makes every msm_bus_scale_update_bw() -
		 * including the (0,0) removal in dev_pm_opp_set_opp(NULL) on
		 * runtime suspend - commit the sleep/wake set via
		 * rpmh_write_batch(). On this SoC that batch commit times out
		 * during GPU collapse and rpmh_rsc_debug() does a BUG().
		 * Active-only keeps updates on the active TCS (rpmh_write) and
		 * avoids that path.
		 */
		paths[i]->handle = msm_bus_scale_register(src, dst,
							  (char *)dev_name(dev),
							  true);
		if (!paths[i]->handle) {
			kfree(paths[i]);
			paths[i] = NULL;
			ret = -EINVAL;
			goto err_rollback;
		}
	}

	*num_paths = count;
	return 0;

err_rollback:
	while (i--) {
		icc_put(paths[i]);
		paths[i] = NULL;
	}

	/*
	 * Leave the caller with an array it can hand straight to a teardown
	 * path rather than a mix of live and freed handles.
	 */
	*num_paths = 0;

	return ret;
}
EXPORT_SYMBOL(of_icc_get);

int icc_set_bw(struct icc_path *path, u64 ab, u64 ib)
{
	if (!path)
		return -EINVAL;

	return msm_bus_scale_update_bw(path->handle, ab, ib);
}
EXPORT_SYMBOL(icc_set_bw);

void icc_put(struct icc_path *path)
{
	if (!path)
		return;

	if (path->handle) {
		msm_bus_scale_update_bw(path->handle, 0, 0);
		msm_bus_scale_unregister(path->handle);
	}

	kfree(path);
}
EXPORT_SYMBOL(icc_put);

static void devm_icc_release(void *data)
{
	struct devm_icc_closure *closure = data;
	u32 i;

	for (i = 0; i < closure->num_paths; i++) {
		icc_put(closure->paths[i]);
		closure->paths[i] = NULL;
	}

	closure->num_paths = 0;
}

int devm_of_icc_get(struct device *dev,
		    struct icc_path **paths,
		    u32 *num_paths)
{
	struct devm_icc_closure *closure;
	size_t alloc_size;
	u32 i;
	int ret;

	ret = of_icc_get(dev, paths, num_paths);
	if (ret)
		return ret;

	/*
	 * The closure keeps its own copy of the handles so its lifetime is
	 * independent of whatever the caller does with its array.
	 */
	alloc_size = sizeof(*closure) +
		     *num_paths * sizeof(struct icc_path *);

	closure = devm_kzalloc(dev, alloc_size, GFP_KERNEL);
	if (!closure) {
		ret = -ENOMEM;
		goto err_cleanup;
	}

	for (i = 0; i < *num_paths; i++)
		closure->paths[i] = paths[i];

	closure->num_paths = *num_paths;

	ret = devm_add_action_or_reset(dev, devm_icc_release, closure);
	if (ret) {
		/* The reset already put every path; just clear the caller's view. */
		goto err_clear;
	}

	return 0;

err_cleanup:
	for (i = 0; i < *num_paths; i++)
		icc_put(paths[i]);

err_clear:
	for (i = 0; i < *num_paths; i++)
		paths[i] = NULL;

	*num_paths = 0;

	return ret;
}
EXPORT_SYMBOL(devm_of_icc_get);
