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
