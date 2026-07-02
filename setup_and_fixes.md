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

---

# CURRENT STATUS:
**MSM module source:** Upstream Linux 5.19
**Kernel version(The one I am on):** 4.19.255 - For Oneplus6/6T by EdwinMoq

### 🟢 Baseline & Core Subsystems
* **MDSS/DPU Pipeline:** Fully functional. Hardware interfaces probe flawlessly, `modetest` queries complete successfully, and early bootloader framebuffer hand-off transitions beautifully into the legacy TTY console (`/dev/fb0`).
* **SMMU Layer:** Stable. IOMMU context banks are mapped and allocated safely.
But most importantly, the panel lights up!

### 🟢 GPU & GMU Status
* **GMU Register Access:** **RESOLVED.** Overcame the blind hard-locking state during `gmu_resume` register reads/writes. Address spacing was incorrect in the device tree blobs (I am so stupid 🙃)
* **Zap shader init:** **FIXED.** On downstream you need `pil_gpu` enabled because that's how the trust zone driver probes pas-id XX and authenticates the zap at boot.
* **DRM Scheduler:** **BACKPORTED & WORKING.** Pulled the 5.19 scheduler core into `scheduler/`. The GPU now actually renders — `kmscube --gears` spins a cube at a locked **60 fps**.
* **DRM SYNCOBJ:** **BACKPORTED** Pulled from 5.19 (alomg with `dma-fence-chain`) and hooked up into `msm_gem_submit.c`

---
---

# FIXES:

## Shims structure:
```
./shims/
├── backports
│   ├── dma-fence-chain.c
│   ├── drm_dsc_helper.c
│   └── drm_syncobj.c
├── compat
│   ├── devm_compat.c
│   └── dma-fence_missing_func.c
├── core
│   ├── drm_missing_func.c
│   ├── drm_shim.c
│   ├── interconnector.c
│   └── opp.c
├── include
│   ├── compat_and_shims
│   │   ├── devm_compat.h
│   │   ├── dma-fence.h
│   │   ├── iommu_shims.h
│   │   ├── msm_compat_clk.h
│   │   ├── nvmem-consumer.h
│   │   ├── reservation.h
│   │   └── xarray_shim.h
│   ├── drm
│   │   ├── display
│   │   │   ├── drm_dp.h
│   │   │   ├── drm_dp_helper.h
│   │   │   ├── drm_dsc.h
│   │   │   └── drm_dsc_helper.h
│   │   ├── drm_shim.h
│   │   ├── drm_syncobj.h
│   │   └── gpu_scheduler.h
│   ├── linux
│   │   ├── adreno-smmu-priv.h
│   │   ├── dma-fence-chain.h
│   │   ├── interconnector.h
│   │   └── opp.h
│   └── uapi
│       ├── uapi_drm.h
│       └── uapi_msm_drm.h
└── NOTE.md
```

## Backports (present in the `shims/` directory):

* `drm_plane_create_blend_mode_property` backported! inside `core/drm_missing_func.c` along with a bunch of other helpers to make it all work.
* `drm_dsc_helpers` were backported from 5.19.
* `drm_syncobj` and all it's helpers were backported from 5.19.

---

## FIXES/added funcs to MSM:

**NOTE:** The detailed explanations are in the actual files.

**Unmanaged CX domain:**
* In `adreno/a6xx_gmu.c` support for CX domain was backported from 6.x. Check `a6xx_gmu_init` flow.

**Why?:** If we don't let the GMU manage the CX rail it will eat through battery life. This patch is from >=6.6 versions.

**Module insmod from userspace:**
* In msm_mdss.c function name `inline int dev_gdsc_enable(struct platform_device *pdev)`
	- used in `msm_mdss_init` in file `msm_mdss.c`
	- used in `a6xx_gmu_init` in file `adreno/a6xx_gmu.c`

**Why?:** To mimic bootloader hand-off.

**Aperture remove conflicing framebuffers:**
* In msm_fbdev.c function name `static inline int msm_aperture_remove_framebuffers()`

**Why?:** We need to remove the existing framebuffer so our DRM/KMS FB can take proper control over it.

**SMMU; NULL TTBR0 and TTBR1 Context faults**
* In `disp/dpu_kms.c` function `_dpu_kms_mmu_init`.
	- Domain is handled differently for downstream. A fix has been set up so if `iommu_get_domain_for_dev` fails you immediately get the upstream fallback.
	- Similar changes to `a6xx_gmu.c`, `a6xx_gpu.c` and `adreno_gpu.c`. Search for `iommu_get_domain_for_dev`-you'll see it.

**Why?:** If we leave it as is and the downstream driver already set the context for us-we will overwrite the table with a NULL table causing:
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

**performance state votes specifically for 0:**
* In `dsi/dsi_host.c` function `dsi_link_clk_disable_6g` added checks for `performance state vote`
* In `disp/dpu_kms.c` function `dpu_runtime_suspend` added checks for `performance state vote`

**Why?:** The downstream OPP helpers don't understand what performance lvl `0` means.

**pixel_clk_src timings:**
* In `dsi/dsi_host.c` function `dsi_link_clk_set_rate_6g`

**Why?** For pre 5.11 versions, we avoid `dev_pm_opp_set_rate()` for `clk_set_rate()` in order to NOT get hit with rounding errors.


**Panel timeout issue specific to ONLY len 8 bytes:**
* In `dsi/dsi_host.c` search for function `static int msm_dsi_create_packet` it acts as a replacement for mipi_dsi_create_packet()

**Why?:** To understand why, we need to learn Android CAF behavior.

---

### Android CAF Inversion Quirk
On Qualcomm Snapdragon platforms, **all DSI command execution utilizes the Command-DMA engine**, regardless of packet length (short 4-byte writes vs. long multi-byte writes).

The CPU writes the packet to system RAM, maps it via the Display SMMU domain over the high-speed AXI interconnect bus (`DISP_CC_MDSS_AXI`), and tells the DMA engine to pull it. The driver then waits for a hardware interrupt signaling completion.

During testing, short writes succeeded, but long writes hit an immediate, unrecoverable `-ETIMEDOUT` hang (`STATUS0` stuck at `CMD_DMA_BUSY`).

#### The Root Cause: Mainline vs Android CAF Array Layout
The upstream 5.19 MSM driver relies on standard Linux core definitions where the MIPI DSI header bytes are arranged sequentially starting at index `0`. However, downstream Android CAF kernels **flipped the byte ordering** of the packet header inside `mipi_dsi_create_packet()`:

| Kernel Tree | `header[0]` | `header[1]` | `header[2]` |
| :--- | :--- | :--- | :--- |
| **Linus Mainline(Inclusive of 4.19)** | Data ID (DI) | Word Count LSB / Param 0 | Word Count MSB / Param 1 |
| **Android CAF 4.19** | Word Count LSB / Param 0 | Word Count MSB / Param 1 | Data ID (DI) |

Because the upstream `dsi_cmd_dma_add()` packed these bytes into the MSM hardware command DWORD assuming mainline ordering, the CAF core helper scrambled the layout. Short writes survived because the DSI engine ignores the Word Count fields for fixed-length short packets. Long writes, however, received a giant garbage Word Count value (e.g., `0x3900`), causing the DMA hardware engine to loop indefinitely waiting for a massive payload that didn't exist.
It's really about understanding the problem, because the fix itself is trivial. All it takes is a simple shim (`msm_dsi_create_packet`) which creates the packet as intended without needing to modify core CAF function.

---

**Complete refactor of msm_gpu_devfreq.c**
* Not really much I can say here. You'll just kinda have to see it for yourself.

**Why?:** The older implementation had plenty of flaws and the were exposed when testing GPU bring-up (Unfortunately I lost the full log-you'll just kinda have to trust me on this one.)
```
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

---

(This was a device tree fix.)

### ⚠️ The Zap Shader / Secure Pipeline Alignment Block
During initial attempts to bring up the engine, the command processor would always fail on a hardware packet submission, throwing a CP opcode error (`possible opcode=0x70E60001`) and caused a time out on the ringbuffer execution.

#### The Root Cause
The upstream driver issues `CP_SET_SECURE_MODE` instructions assuming the GPU's hardware secure pipeline state is managed appropriately. Downstream, this state completely relies on the TrustZone generic Peripheral Image Loader (`qcom,pil-tz-generic`) authenticating the secure Zap shader firmware layer. If the GPU peripheral node is disabled during hardware handoff, the secure pipeline drops into an unauthenticated state, causing the hardware to flat-out reject standard kernel initialization sequences.

#### The Fix
We ensured the platform's peripheral image loader remains fully operational at boot rather than dropping or stubbing it out. Leaving the secure hardware node enabled in the device tree blobs allows the TrustZone layer to safely probe PAS-ID 13 (or whatever ID it may be in your case) and execute early authentication.
**NOTE:** This was the thing that got the GPU to go into idle properly.
---

**DRM Scheduler backport (this is what got the GPU rendering):**
* The 5.19 scheduler core now lives in `scheduler/` (`sched_main.c`, `sched_entity.c`, `sched_fence.c` + `include/drm/gpu_scheduler.h`).

**Why?:** 4.19's in-tree `drm_sched` was too old to map the modern engine job model onto — it would NULL-deref inside `drm_sched_entity_pop_job` the moment real work hit it. Backporting the whole thing is what took the GPU from "idles but doesn't work" to *actually* rendering.

**Random Error(atleast for me):** Doing `CTRL + C` on kmscube for example hangs the device and causes a silent panic.. `pstore` doesn't show any logs either. (maybe because of an IOCTL ?)

**get_vblank_timestamp and get_scanout_position: ** For Pre 5.13 versions, the fixes are now in place. Check `msm_drv.c`, search for `msm_driver_get_scanout_position` and `msm_driver_get_vblank_timestamp`
