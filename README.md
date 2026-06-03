# MSM DRM/KMS 5.19 Backport for Downstream 4.19 Kernels

This project provides a comprehensive backport of the **Qualcomm MSM DRM/KMS driver from Linux 5.19** to the **Downstream kernel bases**. 

It is designed to enable a modern, mainline-aligned graphics stack (DRM/KMS + Adreno) on legacy vendor kernels

**Snapdragon 845 (SDM845)** platform is currently being tested on OnePlus 6/6T `enchilada`/`fajita`.

## Architecture::

This driver is going to a part of **Andrunix** (my main project), and it's going to utilizes a **dual boot.img scheme** for running native Linux on Android hardware:

1.  **Standard Android Boot:** Uses the vendor kernel with KGSL/SDE for regular Android functionality.
2.  **Linux Desktop Boot:** Uses a modified Device Tree (DTB) where vendor KGSL and SDE nodes are stripped and replaced with mainline-aligned MDSS/Adreno nodes, backed by this **msm-drm-5.19** driver.

**NOTE:** This approach might eventually be changed to do a swap while being booted into Android(I have patched SDE's uninit flow-just haven't gotten around to releasing it, yet.)

But for now this approach avoids the extreme complexity of live SDE ↔ MSM driver switching, which is notoriously prone to unfixable teardown race conditions in downstream kernels.

## Key Features

### he Shim Layer (`msm/shims/`)
The core of this project is a sophisticated compatibility layer that bridges the gap between modern kernel APIs and downstream vendor implementations.

*   **Interconnect (ICC) Shim:** Provides a 1:1 mapping of modern `of_icc_get()` and `icc_set_bw()` APIs onto the downstream `msm_bus_scale` framework. Supports both synchronous and asynchronous bandwidth scaling.
*   **OPP (Operating Performance Points) Shim:** A custom implementation of the modern OPP layer. It unified frequency scaling (`clk_set_rate`) and interconnect bandwidth voting into a single `dev_pm_opp_set_opp()` call, matching 5.19 behavior.
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

### GPU (Adreno 630 / A6xx)
*   **GPU/GMU Support:** Integrated mainline A6xx GPU driver with GMU (Graphics Management Unit) support.
*   **CX Power Domain:** Fixed unmanaged CX domain sequencing by backporting 6.x-style `dev_pm_domain_attach_by_name` logic to ensure power is available before any GMU register access. Originally the 5.19 driver didn't manage the CX domain this was ported from 6.x.x.

## mplementation Highlights (Fixes & Hacks)

This backport includes several targeted fixes to address downstream-specific behavior:

*   **Aperture Conflict Resolution:** Implements `msm_aperture_remove_framebuffers()` to cleanly evict the bootloader-initialized simplefb/framebuffer before DRM takes over, preventing memory contention.
*   **Runtime CX Enabling:** Introduced `dev_gdsc_enable()` in `msm_mdss.c` and `a6xx_gmu.c` to allow manual GDSC management when the module is loaded post-boot (insmod).
*   **Performance State Sanitization:** Added logic to ensure performance state votes are correctly reset to 0 during runtime suspend in `dpu_kms.c` and `dsi_host.c`, preventing power leakage.
*   **Dynamic OPP Clamping:** Pixel and byte clocks are dynamically clamped to valid OPP ranges using `msm_dsi_clamp_to_opp`, fixing "stuck" RCGs and incorrect frequency rounding in downstream clock providers.

## Current Status:

**NOTE:** Do NOT expect it to **just work** unless you are on a high enough kernel version. The core driver is functional but it is currently broken for downstream (panel timesout atleast for me. 🙃)

*   **Probing:** Driver probes and initializes fully.
*   **Display:** `modetest` works. (as long as gmu_resume is never touched)Early framebuffer hand-off works.
*   **IOMMU:** Translation and context bank allocation seem stable.
*   **GPU:** GMU register access still causes hangs in `gmu_resume` (gmu_read/gmu_write is broken.)

## 🛠️ Integration

1.  Copy the `msm/` directory into `drivers/gpu/drm/msm/`.
2.  Add the contents of `msm/Kconfig` and `msm/Makefile` to your kernel build system.
3.  Ensure your Device Tree (DT) uses mainline-style compatible strings (e.g., `qcom,sdm845-mdss`, `qcom,adreno-630.2`).
4.  **Note:** Requires manual additions to `struct drm_plane_state` in `include/drm/drm_plane.h` for `pixel_blend_mode` support (see `msm/shims/NOTE.md` for details).

## 📄 Technical Documentation
See [msm/shims/NOTE.md](msm/shims/NOTE.md) for a deep dive into specific implementation hacks, SMMU fault analysis, and comparison with 5.4/4.19 MSM drivers.
