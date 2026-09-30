# Kernel integration

This guide describes how to integrate the backported MSM driver into a kernel
tree. The known working reference is the OnePlus 6 (`enchilada`) on the
downstream 4.19 kernel linked from the [README](README.md). Other platforms and
display blocks are included in the source, but have not all been tested.

Set `KERNEL_SRC` to your kernel source tree.
The build command assumes an in-tree kernel build; add the same `O=...` option
to kernel build commands if you use a separate output directory.

The source files are expected at these paths relative to `drivers/gpu/drm/`:

```text
msm/                         <- this repository's msm/
scheduler/                   <- this repository's scheduler/
panel/panel-samsung-sofef00.c <- this repository's panel/panel-samsung-sofef00.c
```

## Device tree

The MSM driver needs device-tree descriptions for the display, GPU, power
domains, clocks, interconnects, IOMMU, and panel that match your platform. Start
from the mainline MDSS/DPU description for your SoC, then adapt it to your
downstream kernel and device.

The `dtbs/` directory contains reference files for the OnePlus 6 and 6T:

* [`sdm845-oneplus-common.dtsi`](dtbs/sdm845-oneplus-common.dtsi)
* [`sdm845-oneplus-enchilada.dts`](dtbs/sdm845-oneplus-enchilada.dts)
* [`sdm845-oneplus-fajita.dts`](dtbs/sdm845-oneplus-fajita.dts)

These files are copied from the [reference kernel tree](https://github.com/EdwinMoq/android_kernel_oneplus_sdm845/tree/lineage-23.2-4.19). If you use that exact kernel and device, they can serve as a starting point; review the comments and compare them with your current DT before using them.

Pay particular attention to the GPU Zap firmware setup. The downstream
`pil_gpu` node must remain enabled for TrustZone to authenticate the firmware.
The `firmware-name` must also match the format present in your vendor firmware
partition: some devices provide split `.mdt`/`.bXX` files, while others provide
a monolithic `.mbn`. The reference DTS explains the expected directory layout
and the choice between these formats.

For the Linux boot configuration used here, the vendor KGSL and SDE display
nodes are disabled so they do not claim the GPU and display hardware alongside
MSM DRM. Keep the normal Android DT separate if you use the dual-boot setup.

## Kernel patches

The reference 4.19 setup uses these patches from this repository:

```sh
git -C "$KERNEL_SRC" apply "$BACKPORT/patches/drm-atomic-add-pixel-blend-mode-property.patch"
git -C "$KERNEL_SRC" apply "$BACKPORT/patches/clk-qcom-sdm845-add-gdsc-power-domains.patch"
```

The first patch adds the `pixel_blend_mode` field/property support required by
the driver. The second adds SDM845 GDSC power-domain descriptions needed by
this setup. Review each patch against your kernel before applying it; the
second is specific to SDM845.

## DRM scheduler

The GPU scheduler backport is in `scheduler/`. It replaces the older 4.19 DRM
scheduler implementation that cannot handle this MSM driver's job model. Use
it as the kernel tree's DRM scheduler implementation. Do not link the original
4.19 scheduler implementation together with this backport.

Place this repository's `scheduler/` directory at
`$KERNEL_SRC/drivers/gpu/drm/scheduler/`, replacing the old scheduler sources.
The backport's Makefile builds `gpu-sched.o`; make sure the DRM Makefile
includes the scheduler directory:

```make
obj-$(CONFIG_DRM_SCHED) += scheduler/
```

If your kernel does not define `CONFIG_DRM_SCHED`, add a Kconfig entry under
DRM. The scheduler can be built into the kernel (`CONFIG_DRM_SCHED=y`) or as a
module (`CONFIG_DRM_SCHED=m`). If you choose the module, make sure the kernel's
original scheduler implementation is disabled/replaced so there is only one
provider of the scheduler symbols. Check the existing DRM Kconfig and Makefile
first so you do not add duplicate entries.

## MSM driver

Place this repository's `msm/` directory at
`$KERNEL_SRC/drivers/gpu/drm/msm/`. The relative includes in the backported
sources expect this location beneath `drivers/gpu/drm/`; keep the directory
name `msm`. Ensure the parent DRM build files include the driver:

```make
obj-$(CONFIG_DRM_MSM) += msm/
```

and that `drivers/gpu/drm/Kconfig` sources the driver's Kconfig:

```kconfig
source "drivers/gpu/drm/msm/Kconfig"
```

The MSM driver must be built as a module (`CONFIG_DRM_MSM=m`). If your kernel
already has an MSM driver, replace its directory and update the existing
entries rather than adding duplicates. Configure the copied driver through
`menuconfig` under **Device Drivers → Graphics support → DRM**.

The [`config/.config`](config/.config) file is a full configuration captured
from the reference 4.19 kernel, not a drop-in fragment for other kernels. Its
MSM selections differ from the expanded options below (for example, MDP4/MDP5
and the other PHYs are disabled there, while HDMI is enabled). The verified
OnePlus 6 display path is DPU, DSI, and the 10nm PHY.

### Expanded MSM options

These selections compile the broader set of display blocks into the MSM
module. They are not a claim that every block has been tested; the verified
OnePlus 6 setup uses DPU, DSI, and the 10nm PHY.

```text
<M> MSM DRM
[*]   Enable SUDO flag on submits
[*]   Enable support for downstream kernels
[*]   Enable MDP4 support in MSM DRM driver
[*]   Enable MDP5 support in MSM DRM driver
[*]   Enable DPU support in MSM DRM driver
[ ]   Enable DisplayPort support in MSM DRM driver
[ ]   Enable DisplayPort debugging support in MSM DRM driver
[*]   Enable DSI support in MSM DRM driver
[*]     Enable DSI 28nm PHY driver in MSM DRM
[*]     Enable DSI 20nm PHY driver in MSM DRM
[*]     Enable DSI 28nm 8960 PHY driver in MSM DRM
[*]     Enable DSI 14nm PHY driver in MSM DRM
[*]     Enable DSI 10nm PHY driver in MSM DRM
[*]     Enable DSI 7nm PHY driver in MSM DRM
[ ]   Enable HDMI support in MSM DRM driver
```

For the tested OnePlus 6 display, the relevant path is DPU, DSI, and the 10nm
DSI PHY. MDP4, MDP5, and the other PHYs are included but have not been
validated as part of this bringup. HDMI and DisplayPort are not currently
patched.

## Panel driver

The repository includes the OnePlus 6/6T Samsung panel driver in `panel/`.
Place its source at
`$KERNEL_SRC/drivers/gpu/drm/panel/panel-samsung-sofef00.c`, and merge the
entries from this repository's `panel/Kconfig` and `panel/Makefile` into the
kernel's corresponding files. The panel can be built in (`y`) or as a module
(`m`). It is specific to the `samsung,sofef00` panel; other devices need their
own compatible panel driver.

On the downstream 4.19 RPMh implementation, I couldn't really find a good way
to switch to High Power Mode (HPM), which is needed for the OP6 panel. So
patches like this exist:

```c
for (i = 0; i < ARRAY_SIZE(ctx->supplies); i++)
    regulator_set_load(ctx->supplies[i].consumer, 100000);
```

(if you are planning to use this as a reference please check mainline to see
which mode `vdda-supply` is expected to run in for your panel otherwise your
panel will NOT light up!):

## Build

After configuring and preparing the kernel tree, build the MSM driver as a
loadable module. Run from `$KERNEL_SRC`:

```sh
make -j"$(nproc)" M=drivers/gpu/drm/msm modules
```

If the scheduler or panel is configured as a module, build it from `$KERNEL_SRC`
as well:

```sh
make -j"$(nproc)" M=drivers/gpu/drm/scheduler modules
make -j"$(nproc)" M=drivers/gpu/drm/panel modules
```

The panel command may build other panel modules selected in your kernel config
too. Load the scheduler module before MSM when both are modules; module
dependency handling can resolve this when symbols are exported and dependency
information is available.

The module still depends on the matching kernel build tree, configuration,
generated headers, and symbols. This is an in-tree Kbuild integration that
produces a loadable module, rather than a standalone build that can target an
arbitrary kernel tree without preparation.
