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
	count = len / (sizeof(u32) * 2);

	for (i = 0; i < count; i++) {
		u32 src = be32_to_cpu(data[i * 2]);
		u32 dst = be32_to_cpu(data[i * 2 + 1]);

		paths[i] = kzalloc(sizeof(struct icc_path), GFP_KERNEL);
		if (!paths[i]) {
			ret = -ENOMEM;
			goto err_rollback;
		}

		paths[i]->handle = msm_bus_scale_register(src, dst,
							  (char *)dev_name(dev),
							  false);
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
		if (paths[i])
			icc_put(paths[i]);
	}
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

	if (!closure)
		return;

	for (i = 0; i < closure->num_paths; i++) {
		if (closure->paths[i]) {
			icc_put(closure->paths[i]);
			closure->paths[i] = NULL;
		}
	}
	kfree(closure->paths);
}

int devm_of_icc_get(struct device *dev,
		    struct icc_path **paths,
		    u32 *num_paths)
{
	struct devm_icc_closure *closure;
	struct icc_path **tracked_paths;
	u32 i;
	int ret;

	ret = of_icc_get(dev, paths, num_paths);
	if (ret)
		return ret;

	closure = devm_kzalloc(dev, sizeof(*closure), GFP_KERNEL);
	if (!closure)
		goto err_cleanup;

	tracked_paths = kmalloc_array(*num_paths,
				      sizeof(*tracked_paths),
				      GFP_KERNEL);
	if (!tracked_paths)
		goto err_cleanup;

	for (i = 0; i < *num_paths; i++)
		tracked_paths[i] = paths[i];

	closure->paths = tracked_paths;
	closure->num_paths = *num_paths;

	ret = devm_add_action_or_reset(dev, devm_icc_release, closure);
	if (ret)
		return ret;

	return 0;

err_cleanup:
	for (i = 0; i < *num_paths; i++) {
		if (paths[i])
			icc_put(paths[i]);
	}

	return -ENOMEM;
}
EXPORT_SYMBOL(devm_of_icc_get);
