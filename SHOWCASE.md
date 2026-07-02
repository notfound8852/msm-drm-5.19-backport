# 🧊 Showcase — The GPU is Alive

This is the "proof it works" page for the **5.19 MSM DRM/KMS backport** running on a downstream **4.19** kernel.

Platform: **Snapdragon 845** — OnePlus 6 / 6T (`enchilada` / `fajita`).

The panel lights up, the GPU spins up out of idle, and `kmscube --gears` renders a spinning cube at a locked **60 fps** straight through the backported pipeline (GPU submit → DRM scheduler → ringbuffer → KMS flip).

All the userspace output lives here so [`README.md`](README.md) stays a high-level overview and [`NOTE.md`](msm/shims/NOTE.md) stays a technical deep-dive.

Sorry, Ubuntu Touch. 🙃

---

## 🎥 Demo video

> **Coming soon.** The full run, end to end: `insmod`ing `panel_samsung_sofef00.ko` and `msm.ko`, a quick `modetest` to confirm the pipeline is live, then `kmscube --gears` painting the cube. Not recording it tonight 🙃

<!-- embed the video / link here once recorded -->

---

## 🧊 `kmscube --gears` — 60 fps

A spinning cube, gears mode, locked at 60 fps. This is the whole chain working: GPU command submission → the backported DRM scheduler (`scheduler/`) → ringbuffer execution → atomic KMS page flip onto the DSI panel.

```sh
[root@localhost ~]# insmod panel_samsung_sofef00.ko
[root@localhost ~]# insmod msm.ko
[root@localhost ~]# kmscube --gears
Using display 0x5b699d3810 with EGL version 1.5
===================================
EGL information:
  version: "1.5"
  vendor: "Mesa Project"
  client extensions: "EGL_EXT_device_base EGL_EXT_device_enumeration EGL_EXT_device_query EGL_EXT_platform_base EGL_KHR_client_get_all_proc_addresses EGL_EXT_client_extensions EGL_KHR_debug EGL_EXT_platform_device EGL_EXT_explicit_device EGL_EXT_platform_wayland EGL_KHR_platform_wayland EGL_EXT_platform_x11 EGL_KHR_platform_x11 EGL_EXT_platform_xcb EGL_MESA_platform_gbm EGL_KHR_platform_gbm EGL_MESA_platform_surfaceless"
  display extensions: "EGL_ANDROID_blob_cache EGL_ANDROID_native_fence_sync EGL_EXT_buffer_age EGL_EXT_config_select_group EGL_EXT_create_context_robustness EGL_EXT_image_dma_buf_import EGL_EXT_image_dma_buf_import_modifiers EGL_EXT_query_reset_notification_strategy EGL_EXT_surface_compression EGL_IMG_context_priority EGL_KHR_cl_event2 EGL_KHR_config_attribs EGL_KHR_context_flush_control EGL_KHR_create_context EGL_KHR_create_context_no_error EGL_KHR_fence_sync EGL_KHR_get_all_proc_addresses EGL_KHR_gl_colorspace EGL_KHR_gl_renderbuffer_image EGL_KHR_gl_texture_2D_image EGL_KHR_gl_texture_3D_image EGL_KHR_gl_texture_cubemap_image EGL_KHR_image EGL_KHR_image_base EGL_KHR_image_pixmap EGL_KHR_no_config_context EGL_KHR_reusable_sync EGL_KHR_surfaceless_context EGL_EXT_pixel_format_float EGL_KHR_wait_sync EGL_MESA_configless_context EGL_MESA_gl_interop EGL_MESA_image_dma_buf_export EGL_MESA_query_driver EGL_MESA_x11_native_visual_id "
===================================
MESA: warning: Failed to set BO metadata with DRM_MSM_GEM_INFO: -22
OpenGL ES 2.x information:
  version: "OpenGL ES 3.2 Mesa 26.1.3-arch1.2"
  shading language version: "OpenGL ES GLSL ES 3.20"
  vendor: "freedreno"
  renderer: "FD630"
  extensions: "GL_EXT_blend_minmax GL_EXT_multi_draw_arrays GL_EXT_texture_filter_anisotropic GL_EXT_texture_compression_s3tc GL_EXT_texture_compression_dxt1 GL_EXT_texture_compression_rgtc GL_EXT_texture_format_BGRA8888 GL_OES_compressed_ETC1_RGB8_texture GL_OES_depth24 GL_OES_element_index_uint GL_OES_fbo_render_mipmap GL_OES_mapbuffer GL_OES_rgb8_rgba8 GL_OES_standard_derivatives GL_OES_stencil8 GL_OES_texture_3D GL_OES_texture_float GL_OES_texture_float_linear GL_OES_texture_half_float GL_OES_texture_half_float_linear GL_OES_texture_npot GL_OES_vertex_half_float GL_EXT_draw_instanced GL_EXT_texture_sRGB_decode GL_OES_EGL_image GL_OES_depth_texture GL_OES_packed_depth_stencil GL_EXT_texture_type_2_10_10_10_REV GL_NV_conditional_render GL_OES_get_program_binary GL_APPLE_texture_max_level GL_EXT_discard_framebuffer GL_EXT_read_format_bgra GL_EXT_texture_storage GL_NV_pack_subimage GL_NV_texture_barrier GL_EXT_frag_depth GL_NV_fbo_color_attachments GL_OES_EGL_image_external GL_OES_EGL_sync GL_OES_vertex_array_object GL_OES_viewport_array GL_ANGLE_pack_reverse_row_order GL_ANGLE_texture_compression_dxt3 GL_ANGLE_texture_compression_dxt5 GL_EXT_occlusion_query_boolean GL_EXT_robustness GL_EXT_sRGB GL_EXT_texture_rg GL_EXT_unpack_subimage GL_NV_draw_buffers GL_NV_read_buffer GL_NV_read_depth GL_NV_read_depth_stencil GL_NV_read_stencil GL_APPLE_sync GL_EXT_draw_buffers GL_EXT_instanced_arrays GL_EXT_map_buffer_range GL_EXT_shadow_samplers GL_KHR_debug GL_KHR_robustness GL_KHR_texture_compression_astc_hdr GL_KHR_texture_compression_astc_ldr GL_NV_generate_mipmap_sRGB GL_NV_pixel_buffer_object GL_OES_depth_texture_cube_map GL_OES_required_internalformat GL_OES_surfaceless_context GL_EXT_color_buffer_float GL_EXT_debug_label GL_EXT_sRGB_write_control GL_EXT_separate_shader_objects GL_EXT_shader_framebuffer_fetch GL_EXT_shader_group_vote GL_EXT_shader_implicit_conversions GL_EXT_shader_integer_mix GL_EXT_tessellation_point_size GL_EXT_tessellation_shader GL_ANDROID_extension_pack_es31a GL_EXT_base_instance GL_EXT_compressed_ETC1_RGB8_sub_texture GL_EXT_copy_image GL_EXT_draw_buffers_indexed GL_EXT_draw_elements_base_vertex GL_EXT_gpu_shader5 GL_EXT_multi_draw_indirect GL_EXT_polygon_offset_clamp GL_EXT_primitive_bounding_box GL_EXT_render_snorm GL_EXT_shader_io_blocks GL_EXT_texture_border_clamp GL_EXT_texture_buffer GL_EXT_texture_cube_map_array GL_EXT_texture_norm16 GL_EXT_texture_view GL_KHR_blend_equation_advanced GL_KHR_blend_equation_advanced_coherent GL_KHR_context_flush_control GL_KHR_robust_buffer_access_behavior GL_NV_image_formats GL_NV_shader_noperspective_interpolation GL_OES_copy_image GL_OES_draw_buffers_indexed GL_OES_draw_elements_base_vertex GL_OES_gpu_shader5 GL_OES_primitive_bounding_box GL_OES_sample_shading GL_OES_sample_variables GL_OES_shader_io_blocks GL_OES_shader_multisample_interpolation GL_OES_tessellation_point_size GL_OES_tessellation_shader GL_OES_texture_border_clamp GL_OES_texture_buffer GL_OES_texture_cube_map_array GL_OES_texture_stencil8 GL_OES_texture_storage_multisample_2d_array GL_OES_texture_view GL_EXT_blend_func_extended GL_EXT_buffer_storage GL_EXT_float_blend GL_EXT_geometry_point_size GL_EXT_geometry_shader GL_EXT_texture_filter_minmax GL_EXT_texture_sRGB_R8 GL_EXT_texture_sRGB_RG8 GL_KHR_no_error GL_KHR_texture_compression_astc_sliced_3d GL_OES_EGL_image_external_essl3 GL_OES_geometry_point_size GL_OES_geometry_shader GL_OES_shader_image_atomic GL_EXT_clear_texture GL_EXT_clip_cull_distance GL_EXT_conservative_depth GL_EXT_disjoint_timer_query GL_EXT_multisampled_render_to_texture GL_EXT_multisampled_render_to_texture2 GL_EXT_texture_compression_s3tc_srgb GL_MESA_shader_integer_functions GL_EXT_clip_control GL_EXT_color_buffer_half_float GL_EXT_memory_object GL_EXT_memory_object_fd GL_EXT_semaphore GL_EXT_semaphore_fd GL_EXT_texture_compression_bptc GL_EXT_texture_mirror_clamp_to_edge GL_KHR_parallel_shader_compile GL_EXT_EGL_image_storage GL_EXT_shader_framebuffer_fetch_non_coherent GL_MESA_framebuffer_flip_y GL_EXT_demote_to_helper_invocation GL_EXT_depth_clamp GL_EXT_texture_query_lod GL_MESA_sampler_objects GL_EXT_EGL_image_storage_compression GL_EXT_texture_storage_compression GL_MESA_bgra GL_MESA_texture_const_bandwidth GL_EXT_shader_clock "
===================================
Rendered 120 frames in 2.008390 sec (59.749353 fps)
Rendered 240 frames in 4.016652 sec (59.751252 fps)
Rendered 360 frames in 6.025128 sec (59.749771 fps)
Rendered 480 frames in 8.033715 sec (59.748196 fps)
Rendered 600 frames in 10.042205 sec (59.747836 fps)
Rendered 720 frames in 12.050311 sec (59.749495 fps)
Rendered 840 frames in 14.058509 sec (59.750292 fps)
Rendered 960 frames in 16.066528 sec (59.751555 fps)
...
```

---

## 🖥️ Bring-up logs (`dmesg`)

The display + GPU/GMU pipeline coming up clean.

### MDSS/DPU/DSI/DSI_PHY and Panel pipeline:
```sh
[   37.640550] panel_samsung_sofef00: loading out-of-tree module taints kernel.
[   37.649301] panel_samsung_sofef00: module verification failed: signature and/or required key missing - tainting kernel
[   39.424103] adreno 5000000.gpu: Linked as a consumer to 5040000.iommu
[   39.433319] iommu: Adding device 5000000.gpu to group 38
[   39.443489] msm-mdss ae00000.mdss: Linked as a consumer to 15000000.apps-smmu
[   39.452676] iommu: Adding device ae00000.mdss to group 39
[   39.466510] msm_dsi_phy ae94400.dsi-phy: Linked as a consumer to regulator.10
[   39.478618] msm_dsi ae94000.dsi: Linked as a consumer to regulator.37
[   39.488394] panel-oneplus6 ae94000.dsi.0: Linked as a consumer to regulator.25
[   39.497223] panel-oneplus6 ae94000.dsi.0: Linked as a consumer to regulator.75
[   39.505988] panel-oneplus6 ae94000.dsi.0: Linked as a consumer to regulator.76
[   39.515553] msm_dpu ae01000.mdp: bound ae94000.dsi (ops dsi_ops [msm])
[   39.525240] adreno 5000000.gpu: 5000000.gpu supply vdd not found, using dummy regulator
[   39.534035] adreno 5000000.gpu: Linked as a consumer to regulator.0
[   39.542785] adreno 5000000.gpu: 5000000.gpu supply vddcx not found, using dummy regulator
[   39.551611] Frequency QoS not supported on: 4.19.255... using direct target mode
[   39.561228] platform 506a000.gmu: Linked as a consumer to 5040000.iommu
[   39.570491] iommu: Adding device 506a000.gmu to group 40
[   39.580183] platform 506a000.gmu: Linked as a consumer to genpd:0:506a000.gmu
[   39.589364] msm_dpu ae01000.mdp: bound 5000000.gpu (ops a3xx_ops [msm])
[   39.599262] [drm:dpu_kms_hw_init:1132] dpu hardware revision:0x40000000
[   39.608710] [drm] Supports vblank timestamp caching Rev 2 (21.10.2013).
[   39.617401] [drm] Driver supports precise vblank timestamp query.
[   39.627233] [drm] Initialized msm 1.9.0 20130625 for ae01000.mdp on minor 0
[   39.635996] msmdrm: Evicting conflicting fb at 0x000000009d400000 (size: 36864 KB)
[   39.644580] checking generic (9d400000 2400000) vs hw (9d400000 2400000)
[   39.644587] fb: switching to msmdrmfb from simple
```

### GPU/GMU bring-up pipeline:
```sh
[   47.021251] msm_dpu ae01000.mdp: [drm:adreno_request_fw [msm]] loaded qcom/a630_sqe.fw from new location
[   47.021913] msm_dpu ae01000.mdp: [drm:adreno_request_fw [msm]] loaded qcom/a630_gmu.bin from new location
```

---

## 🔍 `modetest` + atomic state

`modetest -M msm -c` enumerating the DSI-1 connector, plus the full plane / CRTC atomic state dump from `/sys/kernel/debug/dri/0/state`.

```sh
[root@localhost ~]# modetest -M msm -M msm -c
opened device `MSM Snapdragon DRM` on driver `msm` (version 1.9.0 at 20130625)
Connectors:
id      encoder status          name            size (mm)       modes   encoders
29      28      connected       DSI-1           68x145          1       28
  modes:
        index name refresh (Hz) hdisp hss hse htot vdisp vss vse vtot
  #0 1080x2280 60.00 1080 1192 1208 1244 2280 2316 2324 2336 174359 flags: ; type: preferred, driver
  props:
        1 EDID:
                flags: immutable blob
                blobs:

                value:
        2 DPMS:
                flags: enum
                enums: On=0 Standby=1 Suspend=2 Off=3
                value: 0
        5 link-status:
                flags: enum
                enums: Good=0 Bad=1
                value: 0
        6 non-desktop:
                flags: immutable range
                values: 0 1
                value: 0

[root@localhost ~]# cat /sys/kernel/debug/dri/0/state
plane[30]: plane-0
        crtc=crtc-0
        fb=81
                allocated by = [fbcon]
                refcount=2
                format=XR24 little-endian (0x34325258)
                modifier=0x0
                size=1080x2280
                layers:
                        size[0]=1080x2280
                        pitch[0]=4352
                        offset[0]=0
                        obj[0]:
                                name=0
                                refcount=1
                                start=00000000
                                size=9924608
                                imported=no
        crtc-pos=1080x2280+0+0
        src-pos=1080.000000x2280.000000+0.000000+0.000000
        rotation=1
        normalized-zpos=0
        color-encoding=ITU-R BT.601 YCbCr
        color-range=YCbCr limited range
        stage=1
        sspp=sspp_0
        multirect_mode=none
        multirect_index=solo
plane[36]: plane-1
        crtc=(null)
        fb=0
        crtc-pos=0x0+0+0
        src-pos=0.000000x0.000000+0.000000+0.000000
        rotation=1
        normalized-zpos=0
        color-encoding=ITU-R BT.601 YCbCr
        color-range=YCbCr limited range
        stage=0
        sspp=sspp_1
        multirect_mode=none
        multirect_index=solo
plane[42]: plane-2
        crtc=(null)
        fb=0
        crtc-pos=0x0+0+0
        src-pos=0.000000x0.000000+0.000000+0.000000
        rotation=1
        normalized-zpos=0
        color-encoding=ITU-R BT.601 YCbCr
        color-range=YCbCr limited range
        stage=0
        sspp=sspp_2
        multirect_mode=none
        multirect_index=solo
plane[48]: plane-3
        crtc=(null)
        fb=0
        crtc-pos=0x0+0+0
        src-pos=0.000000x0.000000+0.000000+0.000000
        rotation=1
        normalized-zpos=0
        color-encoding=ITU-R BT.601 YCbCr
        color-range=YCbCr limited range
        stage=0
        sspp=sspp_3
        multirect_mode=none
        multirect_index=solo
plane[54]: plane-4
        crtc=(null)
        fb=0
        crtc-pos=0x0+0+0
        src-pos=0.000000x0.000000+0.000000+0.000000
        rotation=1
        normalized-zpos=0
        color-encoding=ITU-R BT.601 YCbCr
        color-range=YCbCr limited range
        stage=0
        sspp=sspp_8
        multirect_mode=none
        multirect_index=solo
plane[60]: plane-5
        crtc=(null)
        fb=0
        crtc-pos=0x0+0+0
        src-pos=0.000000x0.000000+0.000000+0.000000
        rotation=1
        normalized-zpos=0
        color-encoding=ITU-R BT.601 YCbCr
        color-range=YCbCr limited range
        stage=0
        sspp=sspp_9
        multirect_mode=none
        multirect_index=solo
plane[66]: plane-6
        crtc=(null)
        fb=0
        crtc-pos=0x0+0+0
        src-pos=0.000000x0.000000+0.000000+0.000000
        rotation=1
        normalized-zpos=0
        color-encoding=ITU-R BT.601 YCbCr
        color-range=YCbCr limited range
        stage=0
        sspp=sspp_10
        multirect_mode=none
        multirect_index=solo
plane[72]: plane-7
        crtc=(null)
        fb=0
        crtc-pos=0x0+0+0
        src-pos=0.000000x0.000000+0.000000+0.000000
        rotation=1
        normalized-zpos=0
        color-encoding=ITU-R BT.601 YCbCr
        color-range=YCbCr limited range
        stage=0
        sspp=sspp_11
        multirect_mode=none
        multirect_index=solo
crtc[78]: crtc-0
        enable=1
        active=1
        planes_changed=1
        mode_changed=0
        active_changed=0
        connectors_changed=0
        color_mgmt_changed=0
        plane_mask=1
        connector_mask=1
        encoder_mask=1
        mode: 0:"1080x2280" 60 174359 1080 1192 1208 1244 2280 2316 2324 2336 0x48 0x0
        lm[0]=0
        ctl[0]=2
connector[29]: DSI-1
        crtc=crtc-0
```
