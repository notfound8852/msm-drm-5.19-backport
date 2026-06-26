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


* drm_plane_create_blend_mode_property backported! inside `backported/drm_func.c`
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

and then in `drivers/gpu/drm/drm_atomic.c` funtion `drm_atomic_plane_set_property`
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
**MSM module from:** 5.19
**Kernel version:** 4.19.255 - For Oneplus6/6T by EdwinMoq

**Baseline:** The driver probes.
* **GPU pipeline:** GMU read/write is cursed. (doing either causes a full system crash and pstore on the next reboot is empty)
	- **Cause:** Unknown.
	- **Observation:** This same issue happens on the backported 5.4 MSM driver. Which is even closer to 4.19 and I'd argue it's **the same without all the assumptions.**

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
`drm_irq_install` is still used which causes issues but it's nothing we can't fix and get the driver to fully init.
After init testing the gmu_resume pipeline shows that, gmu_read/gmu_write causes the same issue this 5.19 driver is causing...

**Ideas:** Thoroughly look at what downstream KGSL does to even work.
## Example:
```
    clock_gpucc: qcom,gpucc@5090000 {
        compatible = "qcom,gpucc-sdm845", "syscon";
        reg = <0x5090000 0x9000>;
        reg-names = "cc_base";
        vdd_cx-supply = <&pm8998_s9_level>;
        vdd_mx-supply = <&pm8998_s6_level>;
        qcom,gpu_cc_gmu_clk_src-opp-handle = <&gmu>;
        #clock-cells = <1>;
        #reset-cells = <1>;
        // as per mainline patch
        #power-domain-cells = <1>;
    };
```

`clock_gpucc` appears to be doing something special on downstream.. it tries to hook `gpu_cc_gmu_clk_src-opp-handle` to the KGSL gmu..
```
		/* Mine */
		mainline_gmu: gmu@506a000 {
			compatible="qcom,adreno-gmu-630.2", "qcom,adreno-gmu";

		/* KGSL */
	    gmu: qcom,gmu {
	        label = "kgsl-gmu";
	        compatible = "qcom,gpu-gmu";

```
As of right now, I don't know what this is, let alone what it could mean.
	- what is it doing?
	- Is this just a special way for KGSL to construct an opp-table?
	- Why is it that this "special" opp-table seems to be nowhere in the device tree? (is it only in memory?)
	- What happens if we stub that line out and force KGSL to probe?

* **MDSS/DPU pipline:** Seems to be working, modetest prints fine as long as we skip `gmu_resume`
    - Panel lights up!

## FIXES/added funcs to MSM:

**Unmanaged CX domain:**
* In `adreno/a6xx_gmu.c` support for CX domain was backported from 6.x. Check `a6xx_gmu_init` flow.

**Module insmod from userspace:**
* In msm_mdss.c function name `inline int dev_gdsc_enable(struct platform_device *pdev)`
	- used in `msm_mdss_init` in file `msm_mdss.c`
	- using in `a6xx_gmu_init` in fikle `adreno/a6xx_gmu.c`

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

**performance state votes specifically for 0:**
* In `dsi/dsi_host.c` function `dsi_link_clk_disable_6g` added checks for `performance state vote`
* In `disp/dpu_kms.c` function `dpu_runtime_suspend` added checks for `performance state vote`

**pixel_clk_src timings:**
* In `dsi/dsi_host.c` dsi_link_clk_set_rate_6g. We avoid `dev_pm_opp_set_rate()` for `clk_set_rate()` downstream's sakes.

**Panel timeout issue specific to ONLY len 8 bytes:**
* In `dsi/dsi_host.c` search for function `static int msm_dsi_create_packet` it acts as a replacement for mipi_dsi_create_packet() since android CAF's are weird.

## Current Logs:

```sh
[   29.087615] panel_samsung_sofef00: loading out-of-tree module taints kernel.
[   29.096408] panel_samsung_sofef00: module verification failed: signature and/or required key missing - tainting kernel
[   31.067232] adreno 0.gpu: Linked as a consumer to 5040000.iommu
[   31.076342] iommu: Adding device 0.gpu to group 38
[   31.086819] msm-mdss ae00000.mdss: Linked as a consumer to 15000000.apps-smmu
[   31.095842] iommu: Adding device ae00000.mdss to group 39
[   31.194880] msm_dsi_phy ae94400.dsi-phy: Linked as a consumer to regulator.10
[   31.205643] msm_dsi ae94000.dsi: Linked as a consumer to regulator.37
[   31.215129] panel-oneplus6 ae94000.dsi.0: Linked as a consumer to regulator.25
[   31.223765] panel-oneplus6 ae94000.dsi.0: Linked as a consumer to regulator.75
[   31.232230] panel-oneplus6 ae94000.dsi.0: Linked as a consumer to regulator.76
[   31.241479] msm_dpu ae01000.mdp: bound ae94000.dsi (ops dsi_ops [msm])
[   31.250836] adreno 0.gpu: 0.gpu supply vdd not found, using dummy regulator
[   31.259238] adreno 0.gpu: Linked as a consumer to regulator.0
[   31.267594] adreno 0.gpu: 0.gpu supply vddcx not found, using dummy regulator
[   31.276000] Frequency QoS not supported on: 4.19.255...
[   31.285520] platform 0.gmu: Linked as a consumer to 5040000.iommu
[   31.294362] iommu: Adding device 0.gmu to group 40
[   31.311890] platform 0.gmu: Linked as a consumer to genpd:0:0.gmu
[   31.328109] msm_dpu ae01000.mdp: bound 0.gpu (ops a3xx_ops [msm])
[   31.345715] [drm:dpu_kms_hw_init:1132] dpu hardware revision:0x40000000
[   31.362135] [drm] Supports vblank timestamp caching Rev 2 (21.10.2013).
[   31.370457] [drm] No driver support for vblank timestamp query.
[   31.378737] [drm] Setting vblank_disable_immediate to false because get_vblank_timestamp == NULL
[   31.388191] [drm] Initialized msm 1.9.0 20130625 for ae01000.mdp on minor 0
[   31.396556] msmdrm: Evicting conflicting fb at 0x000000009d400000 (size: 36864 KB)
[   31.404794] checking generic (9d400000 2400000) vs hw (9d400000 2400000)
[   31.404800] fb: switching to msmdrmfb from simple
[   31.413140] Console: switching to colour dummy device 80x25
[   31.483130] Console: switching to colour frame buffer device 135x142
[   31.509662] msm_dpu ae01000.mdp: fb0: msmdrmfb frame buffer device
```
