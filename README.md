# MSM DRM/KMS 5.19 Backport for Downstream Kernels

This project provides a comprehensive backport of the **Qualcomm MSM DRM/KMS driver from Linux 5.19** to the **Downstream kernel bases**.
**Note:** 4.19 is just the floor. In the future this module will support >=4.19 kernel versions as well.

It is designed to enable a modern, mainline-aligned graphics stack (DRM/KMS + Adreno) on legacy vendor kernels.

**Snapdragon 845 (SDM845)** platform is currently being tested on OnePlus 6/6T `enchilada`/`fajita`.

Here is the kernel I am using. Go check em out: [EdwinMoq](https://github.com/EdwinMoq/android_kernel_oneplus_sdm845/tree/lineage-23.2-4.19)
Kernel version: 4.19.

## Architecture

This driver is going to be a part of **Andrunix** (my main project), and it's going to utilize a **dual boot.img scheme** for running native Linux on Android hardware:

1.  **Standard Android Boot:** Uses the vendor kernel with KGSL/SDE for regular Android functionality.
2.  **Linux Desktop Boot:** Uses a modified Device Tree (DTB) where vendor KGSL and SDE nodes are stripped and replaced with mainline-aligned MDSS/Adreno nodes, backed by this **msm-drm-5.19** driver.

**NOTE:** This approach might eventually be changed to do a live swap while being booted into Android (I have patched SDE's uninit flow — just haven't gotten around to releasing it, yet.)

But for now, this approach avoids the extreme complexity of live SDE ↔ MSM driver switching, which is notoriously prone to unfixable teardown race conditions in downstream kernels.

## Key Features

### The Shim Layer (`msm/shims/`)
The core of this project is a sophisticated compatibility layer that bridges the gap between modern kernel APIs and downstream vendor implementations.

*   **Interconnect (ICC) Shim:** Provides a 1:1 mapping of modern `of_icc_get()` and `icc_set_bw()` APIs onto the downstream `msm_bus_scale` framework. Supports both synchronous and asynchronous bandwidth scaling.
*   **OPP (Operating Performance Points) Shim:** A custom implementation of the modern OPP layer. It unifies frequency scaling (`clk_set_rate`) and interconnect bandwidth voting into a single `dev_pm_opp_set_opp()` call, matching 5.19 behavior.
*   **DRM Helper Backports:** Ported modern DRM core features missing in 4.19:
    *   `drm_plane_create_blend_mode_property`
    *   `drm_writeback_connector_init_with_encoder`
    *   `drm_firmware_drivers_only()` (via raw cmdline parsing)
    *   Atomic plane state reset and reset helpers (`__drm_atomic_helper_plane_reset`).

### Display Pipeline (DPU/DSI)
*   **Mainline DPU Driver:** Ported from 5.19, providing modern plane, CRTC, and encoder management.
*   **DSI PHY & Host:** Full support for 10nm DSI PHYs with mainline-style link/pixel clock management.
*   **OPP-Based Timings:** Implements `msm_dsi_clamp_to_opp` to ensure pixel clocks are correctly clamped to valid OPP ranges, preventing RCG misbehavior common in downstream kernels.
*   **SMMU Fault Fixes:** Resolved translation faults (NULL TTBR0/TTBR1) by implementing robust IOMMU domain fallback logic and context bank handling for downstream SMMU drivers.
*	**Panel Initialization & Signaling:** Resolved downstream-specific panel timeout conditions during the DSI pre-enable/enable sequence, ensuring proper clock/regulator locking before panel handoff.

### GPU (Adreno 630 / A6xx)
*   **CX Power Domain:** Fixed unmanaged CX domain sequencing by backporting 6.x-style `dev_pm_domain_attach_by_name` logic to ensure power is available before any GMU register access. Originally, the 5.19 driver didn't manage the CX domain; this was ported from 6.x.x.
*	**GMU Register Access:** Resolved initial crashes during `gmu_resume` (gmu_read/gmu_write are now functional).
*	**DRM Scheduler:** Backported the 5.19 GPU scheduler core into `scheduler/` so the modern engine job model maps cleanly onto the 4.19 base. This is what took the GPU from "idles forever" to actually executing the ringbuffer and rendering.

## Implementation Highlights (Fixes & Hacks)

This backport includes several targeted fixes to address downstream-specific behavior:

*   **Aperture Conflict Resolution:** Implements `msm_aperture_remove_framebuffers()` to cleanly evict the bootloader-initialized simplefb/framebuffer before DRM takes over, preventing memory contention.
*   **Runtime CX Enabling:** Introduced `dev_gdsc_enable()` in `msm_mdss.c` and `a6xx_gmu.c` to allow manual GDSC management when the module is loaded post-boot (insmod).
*   **Performance State Sanitization:** Added logic to ensure performance state votes are correctly reset to 0 during runtime suspend in `dpu_kms.c` and `dsi_host.c`, preventing power leakage.

## Current Status:

**NOTE:** Do NOT expect it to **just work** unless you are on a high enough kernel version. The core hardware layer is functional — the panel lights up, the GPU spins up out of idle, and it **renders**: `kmscube --gears` at a locked **60 fps** BUT I still wanna make this very clear:
**This is a Work in progress.**

➡️ See **[SHOWCASE.md](SHOWCASE.md)** for the `modetest` / `kmscube --gears` logs, bring-up `dmesg`, and the demo.

*   **Probing:** Driver probes and initializes fully.
*   **Display:** `modetest` works. (`msm_gpu_devfreq.c` was completely refactored.) Early framebuffer hand-off works and the panel does in fact light up.

---

<p align="center">
  <img src="assets/panel_light_up.jpg" alt="OnePlus 6 panel initialized via backported 5.19 MSM DRM driver" width="600">
  <br>
  <em>The OnePlus 6 panel successfully lighting up using the backported mainline display pipeline. </em>
</p>
(I don't really have a good device to take photos, please excuse my terrible photography skills 🙃)

---
---

<p align="center">
  <img src="assets/fbgrab.png" alt="OnePlus 6 DRM/KMS Framebuffer Console Output" width="550">
  <br>
  <em>Raw <code>fbgrab</code> frame buffer dump (Kernel 4.19.255)</em>
</p>

---

*   **IOMMU:** Translation and context bank allocation are stable.
*	**GPU & GMU Core:** GMU register access and `gmu_resume` are functional. The GMU successfully handles its power sequences, register domains are stable, firmware validation passes, and the GPU spins up directly into a clean engine idle loop.
* **DRM Scheduler:** **BACKPORTED & WORKING.** The 5.19 scheduler core now lives in `scheduler/` (`sched_main.c`, `sched_entity.c`, `sched_fence.c`), replacing 4.19's — which was too old to map the modern engine job model onto and would NULL-deref inside `drm_sched_entity_pop_job` the moment real work hit it. With it in place the GPU drains the ringbuffer and renders for real: `kmscube --gears` spins a cube at a locked **60 fps**. 🙃
* **Rendering:** Working. The full chain — GPU submit → DRM scheduler → ringbuffer → atomic KMS flip — is live end to end.


## 🛠️ Integration

1.  Copy the `msm/` directory into `drivers/gpu/drm/msm/`.
2.  Backport the mainline MDSS/DPU Device Tree (DT) for your SoC..
	- You can use mine as a reference check: `dtbs/sdm845-oneplus-common.dtsi`-that's the main backport. `dtbs/sdm845-oneplus-enchilada.dtsi` and `dtbs/sdm845-oneplus-fajita.dtsi` build upon that.
	- **Quick FYI:** The `dtbs` folder in this repo is direct copy of the one from EdwinMoq's kernel repo.
3.  **Note:** Requires manual additions to `struct drm_plane_state` in `include/drm/drm_plane.h` for `pixel_blend_mode` support (see `msm/shims/NOTE.md` for details).
4. For the 5.19 scheduler add these configs to the bottom of the following files, `drivers/gpu/drm/Makefile`:
```
obj-$(CONFIG_DRM_SCHED) += scheduler/
```
Additionally, you might also need to add this to `drivers/gpu/drm/Kconfig`:
```
config DRM_SCHED
    tristate "DRM GPU Scheduler"
    depends on DRM
```

## 📄 Technical Documentation
See [setup_and_fixes.md](setup_and_fixes.md) for a deep dive into specific implementation hacks and SMMU fault analysis along with how to get genpd power-domains to work.
See [SHOWCASE.md](SHOWCASE.md) for the userspace proof — `modetest`, `kmscube --gears` at 60 fps, bring-up `dmesg`, and (eventually) a video of the whole `insmod` → `modetest` → `kmscube` run.
