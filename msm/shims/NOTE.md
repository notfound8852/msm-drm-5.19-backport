## Shims structure:
```
./shims/
│
├── backports							# Mostly unmodified verbatim upstream files
│   ├── dma-fence-chain.c
│   ├── drm_dsc_helper.c
│   └── drm_syncobj.c
│
├── compat								# Small shims, one function or macro per gap
│   ├── devm_compat.c
│   └── dma-fence_missing_func.c
│
├── core								# Substantial shims and reimplementations
│   ├── drm_missing_func.c				# Missing DRM core functions
│   ├── drm_shim.c						# Custom implementations of newer DRM functions
│   ├── interconnector.c				# Custom ICC -> msm-bus translation layer
│   └── opp.c							# Custom atomic OPP scaling & devres engine
│
├── include
│   ├── compat_and_shims				# Headers for ./compat + ./core + ./backports/drm_syncobj.c
│   │   ├── devm_compat.h
│   │   ├── dma-fence.h
│   │   ├── iommu_shims.h
│   │   ├── devm_compat_clk.h
│   │   ├── nvmem-consumer.h
│   │   ├── reservation.h
│   │   └── xarray_shim.h
│   │
│   ├── drm
│   │   ├── display						# Verbatim upstream headers (pairs with backports/)
│   │   │   ├── drm_dp.h
│   │   │   ├── drm_dp_helper.h
│   │   │   ├── drm_dsc.h
│   │   │   └── drm_dsc_helper.h
│   │   │
│   │   ├── drm_shim.h					# Bunch of missing `#define`s
│   │   ├── drm_syncobj.h				# Verbatim upstream headers (pairs with backports/)
│   │   └── gpu_scheduler.h				# This is duplicate of the same header in .../drm/scheduler/include
│   │
│   ├── linux							# Verbatim upstream headers (except for interconnector.h and opp.h)
│   │   ├── adreno-smmu-priv.h
│   │   ├── dma-fence-chain.h
│   │   ├── interconnector.h
│   │   └── opp.h
│   │
│   └── uapi							# Verbatim upstream UAPI headers
│       ├── uapi_drm.h
│       └── uapi_msm_drm.h
│
└── NOTE.md
```

* drm_atomic_helper_dirtyfb in msm_fb.c-we don't have it. I might do something about this sooner or later.

`Interconnector` SHIM usage:
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

**Quick comparisons:**
* **4.19 MSM:**
	- No ICC's.
	- No Power domain handling.
	- iova_pin is used when allocating DSI the TX buffer. (CMA will literally never allocate.)
	- `Iommu` implementation, `pm_runtime_get/put_sync` called before allocation leading to spectacular failure.
	- One Display Processing Unit (DPU) SoC supported. (`sdm845`)

(There are plenty of other issues but this is enough.)

* **5.4 MSM:**
	- ICC exist.
	- PD management exists.
	- iova_pin_and_get is used.
	- iommu don't do the forbidden technique of flipping power every time.
	- 2-3 SoC's are now supported.
`drm_irq_install` is still used which can cause issues. (In my case, it kept returning `-22` even though nothing was wrong.) But it's nothing we can't fix by backporting the `msm_irq_install` function from a version like 5.18.

* **5.18 MSM:**
	- Same initial stuff as 5.4.
	- Broken `init`
		* This specific MSM version seemed to have been transitioning off of `../msm/disp/dpu1/dpu_mdss.c` to `../msm/msm_mdss.c` and Upstream just had made a mistake by removing the `AXI` interconnect vote from the very start to much later along with the oversight of not doing `pm_runtime_enable` immediately causing a collapse mid register read.

## For the future:

### Additional SoC support:
* Add support for A7xx GPU's.. I'll pick 6.16, use the `adreno` directory as a reference and slowly patch it in.
* Add DPU support for more SoC's. Same story, I'll pick 6.16, slowly re-work `dpu_hw_catalog` to support more SoC's..

### Driver Maturing:
* Right now the shims are EXTREMELY rigid. They work on 4.19-sure but 5.4, 5.8, 5.11, 5.16, 5.17(probably where I'll draw the line since, this version is almost identical to 5.19 EXCEPT for drm_dsc_helper) all remain untested..
The shims will need to be ALOT more mature to support all sorts of >=4.19 versions...
