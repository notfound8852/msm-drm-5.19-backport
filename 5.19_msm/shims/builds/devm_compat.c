#include "../devm_compat.h"
#include <linux/export.h>

/**
 * devm_platform_ioremap_resource_byname - call devm_ioremap_resource for
 *					 a platform device, retrieve the
 *					 resource by name
 *
 * @pdev: platform device to use both for memory resource lookup as well as
 *	resource management
 * @name: name of the resource
 *
 * Return: a pointer to the remapped memory or an ERR_PTR() encoded error code
 * on failure.
 */
void __iomem *devm_platform_ioremap_resource_byname(
    struct platform_device *pdev, const char *name)
{
    struct resource *res;

    res = platform_get_resource_byname(pdev, IORESOURCE_MEM, name);
    return devm_ioremap_resource(&pdev->dev, res);
}
EXPORT_SYMBOL(devm_platform_ioremap_resource_byname);

/**
 * component_compare_of - A common component compare function for of_node
 * @dev: component device
 * @data: @compare_data from component_match_add_release()
 *
 * A common compare function when compare_data is device of_node. e.g.
 * component_match_add_release(masterdev, &match, component_release_of,
 * component_compare_of, component_dev_of_node)
 */
int component_compare_of(struct device *dev, void *data) {
    return dev->of_node == data;
}
EXPORT_SYMBOL(component_compare_of);
