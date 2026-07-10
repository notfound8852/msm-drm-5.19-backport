# FIXES:

## Backports (present in the `shims/` directory):

* `drm_plane_create_blend_mode_property` backported! inside `core/drm_missing_func.c` along with a bunch of other helpers to make it all work.
* `drm_dsc_helpers` were backported from 5.19.
* `drm_syncobj` and all it's helpers were backported from 5.19.

(btw, if you wanna see the tree structure of the `shims` directory check `shims/NOTE.md`)

---

## FIXES/added funcs to MSM:

**NOTE:** The detailed explanations are in the actual files.
This is me completely disregarding the amount of version checks in the driver itself. So stuff like:
**get_vblank_timestamp and get_scanout_position: ** For Pre 5.13 versions are in `msm_drv.c`, search for `msm_driver_get_scanout_position` and `msm_driver_get_vblank_timestamp`
**GEM Prime:** `gem_prime_import` and `gem_prime_export` added to `msm_drv.c` in `msm_driver`
**Version checks around newer `const static struct` vs older `static struct`**
...
-is all disregared here. 

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

### ⚠️ The Zap Shader / Secure Pipeline Alignment Block
**Overview:** This was a device tree fix.

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

**MSM_SUBMIT_BO_NO_IMPLICIT: ** In `msm_gem_submit.c` function `submit_fence_sync` we skip sync if userspace wants to opt out..

**Why?:** Modern Mesa expectations...

```sway
00:00:01.120  [seatd/server.c:145] New client connected (pid: 746, uid: 0, gid: 0)
00:00:01.120  [seatd/seat.c:248] Added client 1 to seat0
00:00:01.120  [seatd/seat.c:584] Opened client 1 on seat0
00:00:00.643 [sway/config/output.c:1219] failed to execute 'swaybg' (background configuration probably not applied): No such file or directory
MESA: warning: Failed to set BO metadata with DRM_MSM_GEM_INFO: -22
00:00:00.757 [wlr] [render/vulkan/pass.c:632] vkQueueSubmit: ERROR_DEVICE_LOST (-4)
00:00:00.758 [sway/config/output.c:1060] Building output state for 'DSI-1' failed
00:00:00.758 [wlr] [render/vulkan/renderer.c:625] vkQueueWaitIdle: ERROR_DEVICE_LOST (-4)
dbus-update-activation-environment: error: unable to connect to D-Bus: Using X11 for dbus-daemon autolaunch was disabled at compile time, set your DBUS_SESSION_BUS_ADDRESS instead
Running in chroot, ignoring command 'set-environment'
Running in chroot, ignoring command 'import-environment'
os_same_file_description couldn't determine if two DRM fds reference the same file description. (Function not implemented)
Let's just assume that file descriptors for the same file probablyshare the file description instead. This may cause problems whenthat isn't the case.
00:00:00.847 [wlr] [render/vulkan/renderer.c:1122] vkDeviceWaitIdle: ERROR_DEVICE_LOST (-4)
00:00:00.057 [swaybar/tray/tray.c:43] Failed to connect to user bus: No such file or directory
```

**MSM_INFO_SET_METADATA:** Added this from upstream so now we can set the metadata for BO's.

**Why?:** Nobody wants to see, `MESA: warning: Failed to set BO metadata with DRM_MSM_GEM_INFO: -22`

**msm_sched_job_add_implicit_dependencies:** Added to `msm_gem_submit.c` as a 4.19-5.3-compatible reimplementation of upstream's `drm_sched_job_add_implicit_dependencies`.

**Why?:** Upstream's helper (5.16+) assumes `drm_gem_object` embeds `->resv` directly and walks it with `dma_resv_iter`/`dma_resv_usage_rw`. On 4.19, `msm_gem_object` still carries its own `struct reservation_object`, and fences live behind the legacy `fence_excl`/`fence` (shared list) fields with manual RCU handling — there's no iterator to call. Without this, implicit sync (exclusive fence always a dep, shared fences only on write) just doesn't happen, which bites you the moment two jobs touch the same BO without explicit fencing.
