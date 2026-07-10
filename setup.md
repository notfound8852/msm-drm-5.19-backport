# Quick how-to

## For power domains:

In `drivers/clk/qcom/gpucc-sdm845.c` add these:

```c
#include "gdsc.h"

int gdsc_gx_do_nothing_enable(struct generic_pm_domain *domain)
{
    /* Do nothing with the GDSC itself */
    return 0;
}

static struct gdsc gpu_gx_gdsc = {
        .gdscr = 0x100c,
        .clamp_io_ctrl = 0x1508,
        .pd = {
                .name = "gpu_gx_gdsc",
                .power_on = gdsc_gx_do_nothing_enable,
        },
        .pwrsts = PWRSTS_OFF_ON,
        .flags = CLAMP_IO | AON_RESET | POLL_CFG_GDSCR,
};

static struct gdsc gpu_cx_gdsc = {
        .gdscr = 0x106c,
        .gds_hw_ctrl = 0x1540,
        .pd = {
                .name = "gpu_cx_gdsc",
        },
        .pwrsts = PWRSTS_OFF_ON,
        .flags = VOTABLE,
};

static struct gdsc *gpu_cc_sdm845_gdscs[] = {
        [GPU_CX_GDSC] = &gpu_cx_gdsc,
        [GPU_GX_GDSC] = &gpu_gx_gdsc,
};

static const struct qcom_cc_desc gpu_cc_sdm845_desc = {
    .config = &gpu_cc_sdm845_regmap_config,
    .clks = gpu_cc_sdm845_clocks,
    .num_clks = ARRAY_SIZE(gpu_cc_sdm845_clocks),
    .resets = gpu_cc_sdm845_resets,
    .num_resets = ARRAY_SIZE(gpu_cc_sdm845_resets),
        // Add these:
    .gdscs = gpu_cc_sdm845_gdscs,
    .num_gdscs = ARRAY_SIZE(gpu_cc_sdm845_gdscs),
};
```
Then in `drivers/clk/qcom/dispcc-sdm845.c` add these:

```c
#include "gdsc.h"

static struct gdsc mdss_gdsc = {
    .gdscr = 0x3000,
    .en_few_wait_val = 0x6,
    .en_rest_wait_val = 0x5,
    .pd = {
        .name = "mdss_gdsc",
    },
    .pwrsts = PWRSTS_OFF_ON,
    .flags = HW_CTRL | POLL_CFG_GDSCR,
};

static struct gdsc *disp_cc_sdm845_gdscs[] = {
    [MDSS_GDSC] = &mdss_gdsc,
};

static const struct qcom_cc_desc disp_cc_sdm845_desc = {
    .config = &disp_cc_sdm845_regmap_config,
    .clks = disp_cc_sdm845_clocks,
    .num_clks = ARRAY_SIZE(disp_cc_sdm845_clocks),
    .resets = disp_cc_sdm845_resets,
    .num_resets = ARRAY_SIZE(disp_cc_sdm845_resets),
    // Add these:
    .gdscs = disp_cc_sdm845_gdscs,
    .num_gdscs = ARRAY_SIZE(disp_cc_sdm845_gdscs),
};

```

---

**NOTE:** 4.19 doesn't have `pixel_blend_mode` and `blend_mode_property`

### Quick guide:
The above mentioned fields were manually added inside `struct drm_plane_state` in `include/drm/drm_plane.h`
```
        /**
         * @pixel_blend_mode:
         * The alpha blending equation selection, describing how the pixels from
         * the current plane are composited with the background. Value can be
         * one of DRM_MODE_BLEND_*
         */
        uint16_t pixel_blend_mode;

        /**
         * @blend_mode_property:
         * Optional "pixel blend mode" enum property for this plane.
         * Blend mode property represents the alpha blending equation selection,
         * describing how the pixels from the current plane are composited with
         * the background.
         */
        struct drm_property *blend_mode_property;
```

* Then in `drivers/gpu/drm/drm_atomic.c` function `drm_atomic_plane_set_property`
this was added under alpha:
```
        } else if (property == plane->blend_mode_property) {
                state->pixel_blend_mode = val;
```
then in `drm_atomic_plane_get_property`
under alpha again:
```
        } else if (property == plane->blend_mode_property) {
                *val = state->pixel_blend_mode;
```
That's literally it.
All the other newer helpers for reset and everything were backported painlessly into this driver.
