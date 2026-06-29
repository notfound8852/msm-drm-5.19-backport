#ifndef DEVM_COMPAT_H
#define DEVM_COMPAT_H

#include <linux/platform_device.h>
#include <linux/io.h>

void __iomem *devm_platform_ioremap_resource_byname(
	struct platform_device *pdev, const char *name);

int component_compare_of(struct device *dev, void *data);

#endif /* DEVM_COMPAT_H */
