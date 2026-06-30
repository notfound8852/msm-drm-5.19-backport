# The following were wrapped in KERNEL VERSION CHECKS:
* drm_atomic_helper_dirtyfb in msm_fb.c-we don't have it.
* drm_gem_plane_helper_prepare_fb in disp/*/*_plane.c for drm_gem_fb_prepare_fb (older expectations)
* drm_plane_enable_fb_damage_clips full dropped
* drm_syncobj_add_point was literally replaced with drm_syncobj_replace_fence-Unique Vulkan API's will absolutely explode. (I'll think of something else. 🙃)
* drm_sched_job_cleanup-4.19 doesn't need it.
- dpu_crtc_get_scanout_position UNHANDLED!
- dpu_crtc_get_vblank_counter UNHANDLED!
* `genpd` power-domains (MDSS_GDSC, GPU_GX_GDSC, GPU_CX_GDSC)

## Quick how to:

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

* INTERCONNECTOR SHIM usage:
```
	dummy_device: device@10000000 {
		compatible = "vendor,dummy-device";
		reg = <0x10000000 0x1000>;
		interconnects = <22 512>, <23 512>;
		// OR
		interconnects = <MSM_BUS_MASTER_MDP_PORT0 MSM_BUS_SLAVE_EBI_CH0>,
						<MSM_BUS_MASTER_MDP_PORT1 MSM_BUS_SLAVE_EBI_CH0>;
		status = "okay";
	};
```


* drm_plane_create_blend_mode_property backported! inside `core/drm_missing_func.c`
NOTE: 4.19 doesn't have `pixel_blend_mode` and `blend_mode_property`
these fields were manually added inside `struct drm_plane_state {` in `include/drm/drm_plane.h`
## Example:
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

and then in `drivers/gpu/drm/drm_atomic.c` function `drm_atomic_plane_set_property`
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
that's literally it.
All the other newer helpers for reset and everything were backported painlessly into this driver.

## CURRENT STATUS:
**MSM module source:** Upstream Linux 5.19
**Kernel version(The one I am on):** 4.19.255 - For Oneplus6/6T by EdwinMoq

### 🟢 Baseline & Core Subsystems
* **MDSS/DPU Pipeline:** Fully functional. Hardware interfaces probe flawlessly, `modetest` queries complete successfully, and early bootloader framebuffer hand-off transitions beautifully into the legacy TTY console (`/dev/fb0`).
* **SMMU Layer:** Stable. IOMMU context banks are mapped and allocated safely.
But most importantly, the panel lights up!

### 🟢 GPU & GMU Status
* **GMU Register Access:** **RESOLVED.** Overcame the blind hard-locking state during `gmu_resume` register reads/writes. Address spacing was incorrect in the device tree blobs (I am so stupid 🙃)
* **Zap shader init:** **FIXED.** On downstream you need `pil_gpu` enabled because that's how the trust zone driver probes pas-id XX and authenticates the zap at boot.
* **DRM Scheduler:** **BACKPORTED & WORKING.** 🙃 Pulled the 5.19 scheduler core into `scheduler/`. The GPU now actually renders — `kmscube --gears` spins a cube at a locked **60 fps**.
* **Still on the list:** the `drm_syncobj` timeline situation (the Vulkan APIs). I'll get to it.

**Quick comparisons:**
* **4.19 MSM:**
	- No ICC's.
	- No Power domain handling.
	- iova_pin is used when allocating DSI the TX buffer. (CMA will literally never allocate.)
	- iommu implementation is wrong. pm_runtime_get/put_sync called before allocation leading to spectacular failure.
	- One SoC supported.
(There are plenty other's but this is enough.)
* **5.4 MSM:**
	- ICC exist.
	- PD management exists.
	- iova_pin_and_get is used.
	- iommu don't do the forbidden technique of flipping power every time.
	- 2-3 SoC's are now supported.
`drm_irq_install` is still used which can cause issues. (In my case, it kept returning `-22` even though nothing was wrong.) But it's nothing we can't fix by backporting the `msm_irq_install` function from a version like 5.18.

## FIXES/added funcs to MSM:

**Unmanaged CX domain:**
* In `adreno/a6xx_gmu.c` support for CX domain was backported from 6.x. Check `a6xx_gmu_init` flow.

**Module insmod from userspace:**
* In msm_mdss.c function name `inline int dev_gdsc_enable(struct platform_device *pdev)`
	- used in `msm_mdss_init` in file `msm_mdss.c`
	- used in `a6xx_gmu_init` in file `adreno/a6xx_gmu.c`

**Aperture remove conflicing framebuffers:**
* In msm_fbdev.c function name `static inline int msm_aperture_remove_framebuffers()`

**SMMU; NULL TTBR0 and TTBR1 Context faults**
```
[   29.887544] arm-smmu 15000000.apps-smmu: FAR    = 0x0000000000001000
[   29.887549] arm-smmu 15000000.apps-smmu: PAR    = 0x0000000000000000
[   29.887553] arm-smmu 15000000.apps-smmu: FSR    = 0x40000402 [TF R SS ]
[   29.887557] arm-smmu 15000000.apps-smmu: TTBR0  = 0x0000000000000000
[   29.887560] arm-smmu 15000000.apps-smmu: TTBR1  = 0x0000000000000000
[   29.887563] arm-smmu 15000000.apps-smmu: SCTLR  = 0x00c000e7 ACTLR  = 0x00000103
[   29.887592] arm-smmu 15000000.apps-smmu: CBAR  = 0x0001f300
[   29.887596] arm-smmu 15000000.apps-smmu: MAIR0   = 0xf404ff44 MAIR1   = 0x000000e4
[   29.887615] arm-smmu 15000000.apps-smmu: Unhandled context fault: iova=0x00001000, cb=0, fsr=0x40000402, fsynr0=0x200001, fsynr1=0x0
[   29.887618] arm-smmu 15000000.apps-smmu: Client info: BID=0x0, PID=0x0, MID=0x0
[   29.887622] arm-smmu 15000000.apps-smmu: soft iova-to-phys=0x0000000000000000
[   29.887626] arm-smmu 15000000.apps-smmu: SOFTWARE TABLE WALK FAILED! Looks like 15000000.apps-smmu accessed an unmapped address!
[   29.887628] arm-smmu 15000000.apps-smmu: hard iova-to-phys (ATOS) failed
[   29.887631] arm-smmu 15000000.apps-smmu: SID=0x880
```
* In `disp/dpu_kms.c` function `_dpu_kms_mmu_init`.
	- Domain is handled differently for downstream. A fix has been set up so if `iommu_get_domain_for_dev` fails you immediately get the upstream fallback.
	- Similar changes to `a6xx_gmu.c`, `a6xx_gpu.c` and `adreno_gpu.c`. Search for `iommu_get_domain_for_dev`-you'll see it.

**performance state votes specifically for 0:**
* In `dsi/dsi_host.c` function `dsi_link_clk_disable_6g` added checks for `performance state vote`
* In `disp/dpu_kms.c` function `dpu_runtime_suspend` added checks for `performance state vote`

**pixel_clk_src timings:**
* In `dsi/dsi_host.c` dsi_link_clk_set_rate_6g. We avoid `dev_pm_opp_set_rate()` for `clk_set_rate()` downstream's sakes.

**Panel timeout issue specific to ONLY len 8 bytes:**
* In `dsi/dsi_host.c` search for function `static int msm_dsi_create_packet` it acts as a replacement for mipi_dsi_create_packet() since android CAF's are weird.

**Complete refactor of msm_gpu_devfreq.c**
* Not really much I can say here. You'll just kinda have to see for yourself.

**DRM Scheduler backport (this is what got the GPU rendering):**
* The 5.19 scheduler core now lives in `scheduler/` (`sched_main.c`, `sched_entity.c`, `sched_fence.c` + `include/drm/gpu_scheduler.h`).
* 4.19's in-tree `drm_sched` was too old to map the modern engine job model onto — it would NULL-deref inside `drm_sched_entity_pop_job` the moment real work hit it. Backporting the whole thing is what took the GPU from "idles but doesn't work" to *actually* rendering.

**Random Error(atleast for me):** Doing `CTRL + C` on kmscube for example hangs the device and causes a silent panic.. `pstore` doesn't show any logs either.
