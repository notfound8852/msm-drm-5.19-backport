//#include <drm/drm_property.h>
/*
 * NOTE: This file is completely umused
*/
/**
 * drm_plane_enable_fb_damage_clips - Enables plane fb damage clips property.
 * @plane: Plane on which to enable damage clips property.
 *
 * This function lets driver to enable the damage clips property on a plane.
 */
/*
void drm_plane_enable_fb_damage_clips(struct drm_plane *plane)
{
	struct drm_device *dev = plane->dev;
	struct drm_mode_config *config = &dev->mode_config;

	drm_object_attach_property(&plane->base, config->prop_fb_damage_clips,
				   0);
}

*/
static inline void drm_plane_enable_fb_damage_clips(struct drm_plane *plane) { }
