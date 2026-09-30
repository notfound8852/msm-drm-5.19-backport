# MSM DRM/KMS 5.19 Backport for Downstream Kernels

A backport of the **Mainline MSM DRM/KMS driver from Linux 5.19** to downstream Linux kernels, providing a modern DRM/KMS + Adreno graphics stack on vendor kernel bases.

I have tested older MSM versions but they mostly an experiment.

> **Current status: Working (and being worked on.)**

The driver has been tested end-to-end on a **OnePlus 6 (SDM845)** with a downstream 4.19 kernel, including:

* Full **DRM/KMS** display initialization
* DPU display pipeline
* DSI panel initialization
* Adreno 630 GPU acceleration
* GMU firmware and power sequencing
* DRM GPU scheduler
* DRM syncobjs and timeline synchronization
* Vulkan rendering through **Freedreno/Turnip**
* `modetest`
* `kmscube`
* **Sway**
* **Hyprland**

---

## Current Hardware / Kernel

The primary development platform is:

| Component   | Configuration                                   |
| ----------- | ----------------------------------------------- |
| Device      | OnePlus 6 (`enchilada`) / OnePlus 6T (`fajita`) |
| SoC         | Qualcomm Snapdragon 845 (`SDM845`)              |
| GPU         | Adreno 630 (`A630`)                             |
| Kernel      | Downstream Linux 4.19                           |
| DRM source  | Linux 5.19                                      |
| Panel       | Mainline Linux 6.6 panel driver                 |
| Userspace   | Arch Linux / Andrunix                           |
| Vulkan      | Mesa Freedreno / Turnip                         |
| Compositors | Sway, Hyprland                                  |

The current reference kernel is [EdwinMoq/android_kernel_oneplus_sdm845](https://github.com/EdwinMoq/android_kernel_oneplus_sdm845/tree/lineage-23.2-4.19).

---

# Current Status

## 🟢 DRM/KMS

The modern MSM display stack is operational on the downstream 4.19 base.

* **DPU:** Working
* **DSI:** Working
* **SMMU/IOMMU:** Working
* **Atomic KMS:** Working
* **Panel initialization:** Working
* **Framebuffer hand-off:** Working
* **Atomic presentation:** Working

The driver can take over from the bootloader framebuffer and initialize the panel through the backported mainline display pipeline.

## 🟢 Adreno / GPU

The Adreno 630 stack is fully operational.

* GMU power and register access
* GPU CX/GX power domains
* Zap shader firmware authentication
* GPU ringbuffer submission
* DRM scheduler
* GPU synchronization
* Vulkan rendering through Turnip

`kmscube --gears` renders successfully at 60 FPS, and the GPU is used by Wayland compositors for actual desktop rendering.

## 🟢 Wayland

The backported stack has been tested with both:

* **Sway**
* **Hyprland**

Both compositors are able to use the DRM/KMS device and Vulkan renderer provided by the backported graphics stack.

---

# Supported MSM Display Hardware

The original implementation was focused almost exclusively on **DPU + 10nm DSI for SDM845**.

The MSM module now includes support for:

* **MDP4**
* **MDP5**
* **DPU**
* **DSI**
* **28nm DSI PHY**
* **20nm DSI PHY**
* **28nm 8960 DSI PHY**
* **14nm DSI PHY**
* **10nm DSI PHY**
* **7nm DSI PHY**

DisplayPort and HDMI support are not currently supported, yet.

> Hardware support still depends on your downstream kernel and how you port *your* device-tree descriptions. I wanna be clear that while the driver components are now enabled and patched according to mainline, it does not mean your panel will work. (If mainline doesn't have a working panel for you... you'll kind of have to patch it yourself. 🙃)

---

## Project Scope

The goal of this project is support Kernel versions from 4.19 **till** 5.19.

### Newer hardware support

The stock 5.19 MSM driver isn't nearly enough. Qualcomm's Snapdragon 7s Gen 2, proprietary drivers (`SDE` and `KGSL`) got support around Linux 5.10...

So, support for—

* **Adreno A7xx**
* newer DPU generations
* newer DSI/display hardware

—will be backported in the future.

---

## Shim Architecture

The compatibility infrastructure is organized into three primary categories:

```text
shims/
├── backports/     # Mostly verbatim upstream implementations
├── compat/        # Small compatibility gaps
├── core/          # Substantial shims and custom implementations
├── include/       # Compatibility and upstream headers
└── NOTE.md
```

### `backports/`

Contains functionality directly brought over from newer kernels with minimal modification.

```text
backports/
├── dma-fence-chain.c
├── drm_dsc_helper.c
└── drm_syncobj.c
```

`drm_dsc_helper` is mostly to satisfy 5.19's dependencies. (DSI and DPU)

`dma-fence-chain` is a dependancy for `drm_syncobj`. (`ioctls` of which are handled in `msm_drv.c`)

### `compat/`

Contains small compatibility functions or macros required where the host kernel is missing a particular API.

```text
compat/
├── devm_compat.c
└── dma-fence_missing_func.c
```

### `core/`

Contains the heavy stuff.

```text
core/
├── drm_missing_func.c
├── drm_shim.c
├── interconnector.c
└── opp.c
```

`interconnector.c`, to put simply implements modern interconnects with the msm-bus API. [Read more](msm/Documentation/core/interconnector.md)

`opp.c`, builds upon the `interconnector` shim, the existing OPP helpers and the concept of a 'shrink list' (essentially as we move closer to 5.11 the shim will track less and less stuff) [Read more](msm/Documentation/core/opp.md)

`drm_missing_func.c` is mostly pixel blending and an additional `drm_fb_helper_fill_info` function.

`drm_shim.c` holds custom implementations for `drm_writeback_connector_init_with_encoder` and `drm_firmware_drivers_only`

---

# Additional backports

* `scheduler/` is the GPU scheduler from 5.19 which is a hard requirement in order for this backport to work.
* `panel/` contains a patched OnePlus 6 panel from `KVER 6.6`.
* `dtbs/` specifically `sdm845-oneplus-common.dtsi` which is both a backport of the sdm845 mainline `MDSS/DPU/DSI/DSI_PHY/Panel` and `GPU/GMU` stack *and* some of my own implementations.

---

# Why This Exists

There are several 'established' ways of running Linux on Android hardware:

* Compatibility layers such as `libhybris`
* Complete kernel mainlining
* Virtualization through KVM/AVF (This is by Google)
* Other Android userspace-based solutions like `KGSL patches`, `Mesa for android`, etc.

Here is my take.

> **Keep the downstream kernel, but replace the legacy vendor graphics stack with a modern MSM DRM/KMS stack.**

## Comparison

| Approach | What you get | What it costs |
| :--- | :--- | :--- |
| **libhybris** | Android's blobs, callable from a Linux userspace | **Translation overhead** — every graphics call is routed through an intermediate compatibility layer. For example, standard Linux EGL/GBM calls from a Wayland compositor (like Weston) are intercepted, translated, and marshaled into Android-specific `gralloc` or `hwcomposer` calls before reaching the proprietary blobs, destroying native performance. |
| **AVF / KVM** | A real Linux guest, isolated | **Virtualization overhead** — you're virtualizing an entire secondary kernel just to display a desktop. Good luck getting working GPU passthrough on a mobile SoC (a complete nightmare for the pure `KVM` path). |
| **Full Mainlining** | Real upstream kernel, zero overhead | **No Android** — if your device isn't already mainlined, you're forced to reverse-engineer everything yourself. Otherwise, you're at the mercy of half-baked (sometimes they absolutely do work) community drivers, battery drain, broken hardware keys, and overheating issues. |
| **This Backport** | Real upstream-model driver (Your kernel's stock DRM/KMS + modern 5.19 scheduler), zero overhead, vendor blobs still work | **My sanity.** (not really, it was absolutely worth it.) |

---

# Project Architecture

The project is intended to become part of **Andrunix**, which provides a hybrid Linux distro userspace with stock (or custom) Android.

This development setup uses a dual-boot style architecture hence why SDE and KGSL is disabled in `sdm845-oneplus-common.dtsi`:

1. **Android boot:** vendor kernel + KGSL/SDE for normal Android operation.
2. **Linux boot:** downstream kernel with the vendor graphics nodes replaced by the mainline-aligned MSM DRM/Adreno stack.

Ultimately, the longer-term goal for me is... complicated.

I do wanna do a live swap between the vendor and MSM graphics drivers on the fly to keep at as native as possible (See [`msm-sde-uninit-patches`](https://github.com/notfound8852/msm-sde-uninit-patches) for the working SDE uninitialization patches for 4.19.)

But at the same time, I wanna test Mesa compiled against Bionic to all *this* mainline module to be permanently in charge of the display stack.

---

# Documentation

* [`msm/Documentation/README.md`](msm/Documentation/README.md) — shim architecture and compatibility layer
* [`msm/Documentation/FIXES.md`](msm/Documentation/FIXES.md) — downstream-specific fixes and workarounds
* [`msm/Documentation/SETUP.md`](msm/Documentation/SETUP.md) — setup requirements and kernel integration
* [`SHOWCASE.md`](SHOWCASE.md) — logs and userspace validation
* [`patches/`](patches/) — required host-kernel/device-tree patches
* [`INTEGRATION.md`](INTEGRATION.md) — kernel integration, build, and device-tree notes

For the original development history and reasoning behind the project, see the git history.
