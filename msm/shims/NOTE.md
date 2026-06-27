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
**MSM module source:** Upstream Linux 5.19
**Kernel version(The one I am on):** 4.19.255 - For Oneplus6/6T by EdwinMoq

### 🟢 Baseline & Core Subsystems
* **MDSS/DPU Pipeline:** Fully functional. Hardware interfaces probe flawlessly, `modetest` queries complete successfully, and early bootloader framebuffer hand-off transitions beautifully into the legacy TTY console (`/dev/fb0`).
* **SMMU Layer:** Stable. IOMMU context banks are mapped and allocated safely.
But most importantly, the panel lights up!

### 🟡 GPU & GMU Status (In Progress)
* **GMU Register Access:** **RESOLVED.** Overcame the blind hard-locking state during `gmu_resume` register reads/writes. Address spacing was broken incorrect in the device tree blobs (I am so stupid 🙃)
* **CURRENT HARD STOP:** Pushing past the register block exposes an execution failure downstream during command engine hand-off:

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
	- Similiar changes to a6xx_gmu.c, a6xx_gpu.c. Search for `iommu_get_domain_for_dev`-you'll see it.

**performance state votes specifically for 0:**
* In `dsi/dsi_host.c` function `dsi_link_clk_disable_6g` added checks for `performance state vote`
* In `disp/dpu_kms.c` function `dpu_runtime_suspend` added checks for `performance state vote`

**pixel_clk_src timings:**
* In `dsi/dsi_host.c` dsi_link_clk_set_rate_6g. We avoid `dev_pm_opp_set_rate()` for `clk_set_rate()` downstream's sakes.

**Panel timeout issue specific to ONLY len 8 bytes:**
* In `dsi/dsi_host.c` search for function `static int msm_dsi_create_packet` it acts as a replacement for mipi_dsi_create_packet() since android CAF's are weird.

## Current Logs:

### MDSS/DPU/DSI/DSI_PHY and Panel pipeline:
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

### GPU/GMU bring-up pipeline:
```sh
[   47.021251] msm_dpu ae01000.mdp: [drm:adreno_request_fw [msm]] loaded qcom/a630_sqe.fw from new location
[   47.021913] msm_dpu ae01000.mdp: [drm:adreno_request_fw [msm]] loaded qcom/a630_gmu.bin from new location
[   48.032121] [drm:adreno_idle [msm]] *ERROR* A630: timeout waiting to drain ringbuffer 0 rptr/wptr = C/11
[   48.032340] adreno 5000000.gpu: [drm:a6xx_irq [msm]] *ERROR* gpu fault ring 0 fence 0 status 00800005 rb 0011/0011 ib1 0000000000000000/0000 ib2 0000000000000000/0000
[   48.032429] msm_dpu ae01000.mdp: [drm:adreno_load_gpu [msm]] *ERROR* gpu hw init failed: -22
[   48.032572] adreno 5000000.gpu: CP | opcode error | possible opcode=0x70E60001
[   48.032679] msm_dpu ae01000.mdp: [drm:recover_worker [msm]] *ERROR* A630: hangcheck recover!
[   48.032930] Unable to handle kernel NULL pointer dereference at virtual address 0000000000000010
[   48.032983] Mem abort info:
[   48.033004]   ESR = 0x96000005
[   48.033028]   Exception class = DABT (current EL), IL = 32 bits
[   48.033063]   SET = 0, FnV = 0
[   48.033085]   EA = 0, S1PTW = 0
[   48.033107] Data abort info:
[   48.033128]   ISV = 0, ISS = 0x00000005
[   48.033154]   CM = 0, WnR = 0
[   48.033179] user pgtable: 4k pages, 39-bit VAs, pgdp = 000000009e40f346
[   48.033218] [0000000000000010] pgd=0000000000000000, pud=0000000000000000
[   48.033263] Internal error: Oops: 96000005 [#1] PREEMPT SMP
...
```

### Modetest (when GMU/GPU is skipped) and state

```sh
[root@localhost ~]# modetest -M msm -M msm -c
opened device `MSM Snapdragon DRM` on driver `msm` (version 1.9.0 at 20130625)
Connectors:
id      encoder status          name            size (mm)       modes   encoders
29      28      connected       DSI-1           68x145          1       28
  modes:
        index name refresh (Hz) hdisp hss hse htot vdisp vss vse vtot
  #0 1080x2280 60.00 1080 1192 1208 1244 2280 2316 2324 2336 174359 flags: ; type: preferred, driver
  props:
        1 EDID:
                flags: immutable blob
                blobs:

                value:
        2 DPMS:
                flags: enum
                enums: On=0 Standby=1 Suspend=2 Off=3
                value: 0
        5 link-status:
                flags: enum
                enums: Good=0 Bad=1
                value: 0
        6 non-desktop:
                flags: immutable range
                values: 0 1
                value: 0

[root@localhost ~]# cat /sys/kernel/debug/dri/0/state
plane[30]: plane-0
        crtc=crtc-0
        fb=81
                allocated by = [fbcon]
                refcount=2
                format=XR24 little-endian (0x34325258)
                modifier=0x0
                size=1080x2280
                layers:
                        size[0]=1080x2280
                        pitch[0]=4352
                        offset[0]=0
                        obj[0]:
                                name=0
                                refcount=1
                                start=00000000
                                size=9924608
                                imported=no
        crtc-pos=1080x2280+0+0
        src-pos=1080.000000x2280.000000+0.000000+0.000000
        rotation=1
        normalized-zpos=0
        color-encoding=ITU-R BT.601 YCbCr
        color-range=YCbCr limited range
        stage=1
        sspp=sspp_0
        multirect_mode=none
        multirect_index=solo
plane[36]: plane-1
        crtc=(null)
        fb=0
        crtc-pos=0x0+0+0
        src-pos=0.000000x0.000000+0.000000+0.000000
        rotation=1
        normalized-zpos=0
        color-encoding=ITU-R BT.601 YCbCr
        color-range=YCbCr limited range
        stage=0
        sspp=sspp_1
        multirect_mode=none
        multirect_index=solo
plane[42]: plane-2
        crtc=(null)
        fb=0
        crtc-pos=0x0+0+0
        src-pos=0.000000x0.000000+0.000000+0.000000
        rotation=1
        normalized-zpos=0
        color-encoding=ITU-R BT.601 YCbCr
        color-range=YCbCr limited range
        stage=0
        sspp=sspp_2
        multirect_mode=none
        multirect_index=solo
plane[48]: plane-3
        crtc=(null)
        fb=0
        crtc-pos=0x0+0+0
        src-pos=0.000000x0.000000+0.000000+0.000000
        rotation=1
        normalized-zpos=0
        color-encoding=ITU-R BT.601 YCbCr
        color-range=YCbCr limited range
        stage=0
        sspp=sspp_3
        multirect_mode=none
        multirect_index=solo
plane[54]: plane-4
        crtc=(null)
        fb=0
        crtc-pos=0x0+0+0
        src-pos=0.000000x0.000000+0.000000+0.000000
        rotation=1
        normalized-zpos=0
        color-encoding=ITU-R BT.601 YCbCr
        color-range=YCbCr limited range
        stage=0
        sspp=sspp_8
        multirect_mode=none
        multirect_index=solo
plane[60]: plane-5
        crtc=(null)
        fb=0
        crtc-pos=0x0+0+0
        src-pos=0.000000x0.000000+0.000000+0.000000
        rotation=1
        normalized-zpos=0
        color-encoding=ITU-R BT.601 YCbCr
        color-range=YCbCr limited range
        stage=0
        sspp=sspp_9
        multirect_mode=none
        multirect_index=solo
plane[66]: plane-6
        crtc=(null)
        fb=0
        crtc-pos=0x0+0+0
        src-pos=0.000000x0.000000+0.000000+0.000000
        rotation=1
        normalized-zpos=0
        color-encoding=ITU-R BT.601 YCbCr
        color-range=YCbCr limited range
        stage=0
        sspp=sspp_10
        multirect_mode=none
        multirect_index=solo
plane[72]: plane-7
        crtc=(null)
        fb=0
        crtc-pos=0x0+0+0
        src-pos=0.000000x0.000000+0.000000+0.000000
        rotation=1
        normalized-zpos=0
        color-encoding=ITU-R BT.601 YCbCr
        color-range=YCbCr limited range
        stage=0
        sspp=sspp_11
        multirect_mode=none
        multirect_index=solo
crtc[78]: crtc-0
        enable=1
        active=1
        planes_changed=1
        mode_changed=0
        active_changed=0
        connectors_changed=0
        color_mgmt_changed=0
        plane_mask=1
        connector_mask=1
        encoder_mask=1
        mode: 0:"1080x2280" 60 174359 1080 1192 1208 1244 2280 2316 2324 2336 0x48 0x0
        lm[0]=0
        ctl[0]=2
connector[29]: DSI-1
        crtc=crtc-0
```
