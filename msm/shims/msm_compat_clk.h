#ifndef MSM_COMPAT_CLK_H
#define MSM_COMPAT_CLK_H

#include <linux/clk-provider.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/slab.h>
//#include <linux/clk.h>

/* ------------------------------------------------------------------ *
 *  devm_clk_hw_register_divider
 * ------------------------------------------------------------------ */
static void _devm_clk_hw_divider_release(struct device *dev, void *res)
{
    clk_hw_unregister_divider(*(struct clk_hw **)res);
}

static inline struct clk_hw *
devm_clk_hw_register_divider(struct device *dev, const char *name,
                  const char *parent_name, unsigned long flags,
                  void __iomem *reg, u8 shift, u8 width,
                  u8 clk_divider_flags, spinlock_t *lock)
{
    struct clk_hw **ptr, *hw;

    ptr = devres_alloc(_devm_clk_hw_divider_release, sizeof(*ptr), GFP_KERNEL);
    if (!ptr)
        return ERR_PTR(-ENOMEM);

    hw = clk_hw_register_divider(dev, name, parent_name, flags,
                     reg, shift, width, clk_divider_flags, lock);
    if (!IS_ERR(hw)) {
        *ptr = hw;
        devres_add(dev, ptr);
    } else {
        devres_free(ptr);
    }
    return hw;
}

/* ------------------------------------------------------------------ *
 *  devm_clk_hw_register_fixed_factor
 * ------------------------------------------------------------------ */
static void _devm_clk_hw_fixed_factor_release(struct device *dev, void *res)
{
    clk_hw_unregister_fixed_factor(*(struct clk_hw **)res);
}

static inline struct clk_hw *
devm_clk_hw_register_fixed_factor(struct device *dev, const char *name,
                   const char *parent_name, unsigned long flags,
                   unsigned int mult, unsigned int div)
{
    struct clk_hw **ptr, *hw;

    ptr = devres_alloc(_devm_clk_hw_fixed_factor_release, sizeof(*ptr), GFP_KERNEL);
    if (!ptr)
        return ERR_PTR(-ENOMEM);

    hw = clk_hw_register_fixed_factor(dev, name, parent_name, flags, mult, div);
    if (!IS_ERR(hw)) {
        *ptr = hw;
        devres_add(dev, ptr);
    } else {
        devres_free(ptr);
    }
    return hw;
}

/* ------------------------------------------------------------------ *
 *  devm_clk_hw_register_mux
 * ------------------------------------------------------------------ */
static void _devm_clk_hw_mux_release(struct device *dev, void *res)
{
    clk_hw_unregister_mux(*(struct clk_hw **)res);
}

static inline struct clk_hw *
devm_clk_hw_register_mux(struct device *dev, const char *name,
              const char **parent_names, u8 num_parents,
              unsigned long flags, void __iomem *reg, u8 shift,
              u8 mask, u8 clk_mux_flags, spinlock_t *lock)
{
    struct clk_hw **ptr, *hw;

    ptr = devres_alloc(_devm_clk_hw_mux_release, sizeof(*ptr), GFP_KERNEL);
    if (!ptr)
        return ERR_PTR(-ENOMEM);

    hw = clk_hw_register_mux(dev, name, parent_names, num_parents,
                  flags, reg, shift, mask, clk_mux_flags, lock);
    if (!IS_ERR(hw)) {
        *ptr = hw;
        devres_add(dev, ptr);
    } else {
        devres_free(ptr);
    }
    return hw;
}

/* ------------------------------------------------------------------ *
 *  devm_clk_bulk_get_all
 * ------------------------------------------------------------------ */
static inline int devm_clk_bulk_get_all(struct device *dev, struct clk_bulk_data **bulk)
{
    struct property *prop;
    const char *name;
    struct clk_bulk_data *local;
    int i = 0, ret, count;

    count = of_property_count_strings(dev->of_node, "clock-names");
    if (count < 1)
        return count = -EINVAL ? 0 : count; // Return 0 if property missing, or error code

    // FIX: Allocate the correct size for the structure array
    local = devm_kcalloc(dev, count, sizeof(*local), GFP_KERNEL);
    if (!local)
        return -ENOMEM;

    of_property_for_each_string(dev->of_node, "clock-names", prop, name) {
        local[i].id = devm_kstrdup(dev, name, GFP_KERNEL);
        if (!local[i].id) {
            // Unwind previously allocated strings in this invocation
            while (i--) {
                devm_kfree(dev, (void *)local[i].id);
            }
            devm_kfree(dev, local);
            return -ENOMEM;
        }
        i++;
    }

    ret = devm_clk_bulk_get(dev, count, local);
    if (ret) {
        // Unwind everything on clk_get failure
        for (i = 0; i < count; i++) {
            devm_kfree(dev, (void *)local[i].id);
        }
        devm_kfree(dev, local);
        return ret;
    }

    *bulk = local;
    return count;
}
/* ------------------------------------------------------------------ *
 *  devm_clk_bulk_get_optional
 * ------------------------------------------------------------------ */
static inline int devm_clk_bulk_get_optional(struct device *dev, int num_clks,
                                              struct clk_bulk_data *clks)
{
    int ret = devm_clk_bulk_get(dev, num_clks, clks);
    if (ret == -ENOENT) {
        /* clock not present in DT, that's fine */
        return 0;
    }
    return ret;
}

#endif /* MSM_COMPAT_CLK_H */
