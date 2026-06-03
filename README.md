
# MSM DRM/KMS 5.19 Driver Backport to a 4.19 Kernel Base(This is will be scalable in the future DW)

This repository contains a heavy backport of the Qualcomm Snapdragon MSM DRM video driver from Linux kernel 5.19, adapted to compile and execute stably on a downstream kernels architecture (specifically being tested on the Snapdragon 845 / OnePlus 6 `enchilada` platform running Android 16 / CrDOS).

## Current Milestone Status
* **ICC Shims:** Extremely functional, support both async and sync scalling.
* **OPP:** Scales clks, ICC's, voltage.
* **SMMU Layer:** Functional. Resolved context bank allocation mismatches and translation faults between downstream vendor IOMMU behaviors and 5.19 expectations.
* **Early Framebuffer Hand-off:** Functional. Clean transition from continuous splash/LK bootloader display state to the DRM driver node.
* **GMU / GPU Power State Staging:** In Progress. The GMU is completely broken cause iono.

## Integration / How to Use
Drop the `drivers/gpu/drm/msm` tree directly into your kernel source root. Note that you may need to manually expose certain atomic rendering helpers or backport the core DRM helper functions if your host 4.19 tree has heavily modified vendor structs.

## Current Status:

The driver isn't functional... Yet.

Read: [this](https://github.com/notfound8852/msm-drm-5.19-backport/blob/main/msm/shims/NOTE.md) It'll tell you alot.
