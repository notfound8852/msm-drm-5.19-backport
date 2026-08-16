#include <linux/device.h>
#include <linux/slab.h>
#include <linux/export.h>

#include "linux/interconnector.h"

#if !HAS_BASIC_ICC
int of_icc_get_count(struct device *dev)
{
	const __be32 *data;
	int len;

	if (!dev || !dev->of_node)
		return 0;

	data = of_get_property(dev->of_node, "interconnects", &len);
	if (!data || len <= 0 || len % (sizeof(u32) * 2))
		return 0;

	return len / (sizeof(u32) * 2);
}
EXPORT_SYMBOL(of_icc_get_count);

struct icc_path *of_icc_get_by_index(struct device *dev, int idx)
{
	struct icc_path *path;
	const __be32 *data;
	u32 src, dst;
	int len, count;

	if (!dev || !dev->of_node)
		return ERR_PTR(-ENODEV);

	/*
	 * A node with no "interconnects" gets a NULL path rather than an error.
	 * That is what lets a consumer treat bandwidth voting as optional.
	 */
	data = of_get_property(dev->of_node, "interconnects", &len);
	if (!data)
		return NULL;

	/* Each entry = <src dst>; a stray cell means the property is junk. */
	if (len <= 0 || len % (sizeof(u32) * 2)) {
		dev_err(dev,
			"malformed \"interconnects\": expected whole <src dst> pairs\n");
		return ERR_PTR(-EINVAL);
	}

	count = len / (sizeof(u32) * 2);
	if (idx < 0 || idx >= count)
		return ERR_PTR(-ENOENT);

	src = be32_to_cpu(data[idx * 2]);
	dst = be32_to_cpu(data[idx * 2 + 1]);

	path = kzalloc(sizeof(*path), GFP_KERNEL);
	if (!path)
		return ERR_PTR(-ENOMEM);

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
	path->handle = msm_bus_scale_register(src, dst,
					      (char *)dev_name(dev),
					      true);
	if (!path->handle) {
		dev_err(dev, "msm-bus rejected path <%u %u>\n", src, dst);
		kfree(path);
		return ERR_PTR(-EINVAL);
	}

	return path;
}
EXPORT_SYMBOL(of_icc_get_by_index);

struct icc_path *of_icc_get(struct device *dev, const char *name)
{
	struct device_node *np;
	int idx = 0;

	if (!dev || !dev->of_node)
		return ERR_PTR(-ENODEV);

	np = dev->of_node;

	if (!of_find_property(np, "interconnects", NULL))
		return NULL;

	if (name) {
		idx = of_property_match_string(np, "interconnect-names", name);
		if (idx < 0)
			return ERR_PTR(idx);
	}

	return of_icc_get_by_index(dev, idx);
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
#endif /* !HAS_BASIC_ICC */

#if !HAS_ADVANCE_ICC
static void devm_icc_release(struct device *dev, void *res)
{
	icc_put(*(struct icc_path **)res);
}

struct icc_path *devm_of_icc_get(struct device *dev, const char *name)
{
	struct icc_path **ptr, *path;

	ptr = devres_alloc(devm_icc_release, sizeof(*ptr), GFP_KERNEL);
	if (!ptr)
		return ERR_PTR(-ENOMEM);

	path = of_icc_get(dev, name);
	if (!IS_ERR(path)) {
		*ptr = path;
		devres_add(dev, ptr);
	} else {
		devres_free(ptr);
	}

	return path;
}
EXPORT_SYMBOL(devm_of_icc_get);
#endif /* !HAS_ADVANCE_ICC */
