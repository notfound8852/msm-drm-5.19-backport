# The following were wrapped in KERNEL VERSION CHECKS:
* drm_atomic_helper_dirtyfb in msm_fb.c-we don't have it.
* drm_gem_plane_helper_prepare_fb in disp/*/*_plane.c for drm_gem_fb_prepare_fb (older expectations)
* drm_plane_enable_fb_damage_clips full dropped

INTERCONNECTOR SHIM usage:
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
	- iommu implementation is wrong. pm_runtime_get/put_sync called before allocation leading to spectacular failure.
	- One SoC supported.
(There are plenty other's but this is enough.)
* **5.4 MSM:**
	- ICC exist.
	- PD management exists.
	- iova_pin_and_get is used.
	- iommu don't do the forbidden technique of flipping power every time.
	- 2-3 SoC's are now supported.
`drm_irq_install` is still used which can cause issues. (In my case, it kept returning `-22` even though nothing was wrong.) But it's nothing we can't fix by backporting the `msm_irq_install` function from a version like 5.18.

**Random Erro:** Doing `CTRL + C` on kmscube for example hangs the device and causes a silent panic.. `pstore` doesn't show any logs either.
likely cause:
```
[  159.073140] platform 506a000.gmu: [drm:a6xx_gmu_set_oob [msm]] *ERROR* Timeout waiting for GMU OOB set BOOT_SLUMBER: 0x0
[  159.083495] platform 506a000.gmu: [drm:a6xx_gmu_set_oob [msm]] *ERROR* Timeout waiting for GMU OOB set GPU_SET: 0x0
[  160.080145] [drm:adreno_idle [msm]] *ERROR* A630: timeout waiting to drain ringbuffer 0 rptr/wptr = 0/C
[  160.090326] platform 506a000.gmu: [drm:a6xx_gmu_set_oob [msm]] *ERROR* Timeout waiting for GMU OOB set GPU_SET: 0x0
[  160.768132] msm_dpu ae01000.mdp: [drm:hangcheck_handler [msm]] *ERROR* A630: hangcheck detected gpu lockup rb 0!
[  160.768243] msm_dpu ae01000.mdp: [drm:hangcheck_handler [msm]] *ERROR* A630:     completed fence: 9
[  160.768350] msm_dpu ae01000.mdp: [drm:hangcheck_handler [msm]] *ERROR* A630:     submitted fence: 11
[  161.088114] [drm:adreno_idle [msm]] *ERROR* A630: timeout waiting to drain ringbuffer 0 rptr/wptr = 0/C
[  161.088248] msm_dpu ae01000.mdp: [drm:recover_worker [msm]] *ERROR* A630: hangcheck recover!
[  161.088381] msm_dpu ae01000.mdp: [drm:recover_worker [msm]] *ERROR* A630: offending task: Xorg (/usr/lib/Xorg -verbose)
[  161.138052] revision: 630 (6.3.0.2)
[  161.138062] rb 0: fence:    10/12
[  161.138069] rptr:     0
[  161.138074] rb wptr:  50
[  161.138176] adreno 5000000.gpu: [drm:a6xx_recover [msm]] CP_SCRATCH_REG0: 0
[  161.138274] adreno 5000000.gpu: [drm:a6xx_recover [msm]] CP_SCRATCH_REG1: 0
[  161.138371] adreno 5000000.gpu: [drm:a6xx_recover [msm]] CP_SCRATCH_REG2: 0
[  161.138467] adreno 5000000.gpu: [drm:a6xx_recover [msm]] CP_SCRATCH_REG3: 0
[  161.138562] adreno 5000000.gpu: [drm:a6xx_recover [msm]] CP_SCRATCH_REG4: 0
[  161.138658] adreno 5000000.gpu: [drm:a6xx_recover [msm]] CP_SCRATCH_REG5: 0
[  161.138754] adreno 5000000.gpu: [drm:a6xx_recover [msm]] CP_SCRATCH_REG6: 0
[  161.138849] adreno 5000000.gpu: [drm:a6xx_recover [msm]] CP_SCRATCH_REG7: 0
```

## For the future:

### Additional SoC support:
* Add support for A7xx GPU's.. I'll pick 6.16, take the adreno and patch it in.
* Add DPU support for more SoC's. Same story, I'll pick 6.16, slowly re-work `dpu_hw_catalog` to support more SoC's..

### Driver Maturing:
* Right now the shims are EXTREMELY rigid. They work on 4.19-sure but 5.4, 5.8, 5.11, 5.16, 5.17(probably where I'll draw the line since, this version is almost identical to 5.19 EXCEPT for drm_dsc_helper) all remain untested..
The shims will need to be ALOT more mature to support all sorts of >=4.19 versions...
