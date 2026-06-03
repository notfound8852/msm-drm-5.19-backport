#ifndef DRM_SHIM_H
#define DRM_SHIM_H /* 5.18+ capat for 4.19 */

#include <generated/uapi/linux/version.h>

#if LINUX_VERSION_CODE <= KERNEL_VERSION(5, 4, 0)

#ifndef DRM_SCHED_PRIORITY_HIGH
#define DRM_SCHED_PRIORITY_HIGH DRM_SCHED_PRIORITY_HIGH_HW
#endif

#ifndef DRM_SCHED_PRIORITY_COUNT
#define DRM_SCHED_PRIORITY_COUNT DRM_SCHED_PRIORITY_MAX
#endif

#ifndef DRM_MODE_BLEND_PIXEL_NONE
#define DRM_MODE_BLEND_PIXEL_NONE       0
#define DRM_MODE_BLEND_PREMULTI         1
#define DRM_MODE_BLEND_COVERAGE         2
#endif

#ifndef MIPI_DSI_MODE_VIDEO_NO_HFP
#define MIPI_DSI_MODE_VIDEO_NO_HFP    BIT(11)
#define MIPI_DSI_MODE_VIDEO_NO_HBP    BIT(12)
#define MIPI_DSI_MODE_VIDEO_NO_HSA    BIT(13)
#define MIPI_DSI_MODE_NO_EOT_PACKET   BIT(14)
#endif

#ifndef PHY_TYPE_CPHY
#define PHY_TYPE_CPHY 2 // Standard value in newer kernels
#endif
#ifndef DRM_DBG_VBL
#define drm_dbg_vbl(drm, fmt, ...) drm_dbg(DRM_UT_VBL, fmt, ##__VA_ARGS__)
#define drm_dbg_state(drm, fmt, ...) drm_dbg(DRM_UT_ATOMIC, fmt, ##__VA_ARGS__)
#define drm_debug_enabled(category) (drm_debug & (category))
#endif

#ifndef DRM_FORMAT_MAX_PLANES
#define DRM_FORMAT_MAX_PLANES 4
#endif


#ifndef DRM_GEM_PLANE_HELPER_PREPARE_FB
#define drm_gem_plane_helper_prepare_fb(plane, new_state) \
    drm_gem_fb_prepare_fb(plane, new_state)
#endif

#endif /* LINUX_VERSION_CODE */
#endif /* DRM_SHIM_H */
