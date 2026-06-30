#include <drm/drmP.h>
#include <drm/drm_plane.h>
#include <linux/kernel.h>

/*
 * These are full copy-pastes of newer functions..
*/
#ifndef DRM_MODE_BLEND_PIXEL_NONE
#define DRM_MODE_BLEND_PIXEL_NONE       0
#define DRM_MODE_BLEND_PREMULTI         1
#define DRM_MODE_BLEND_COVERAGE         2
#endif

int drm_plane_create_blend_mode_property(struct drm_plane *plane,
					 unsigned int supported_modes)
{
	struct drm_device *dev = plane->dev;
	struct drm_property *prop;
	static const struct drm_prop_enum_list props[] = {
		{ DRM_MODE_BLEND_PIXEL_NONE, "None" },
		{ DRM_MODE_BLEND_PREMULTI, "Pre-multiplied" },
		{ DRM_MODE_BLEND_COVERAGE, "Coverage" },
	};
	unsigned int valid_mode_mask = BIT(DRM_MODE_BLEND_PIXEL_NONE) |
				       BIT(DRM_MODE_BLEND_PREMULTI)   |
				       BIT(DRM_MODE_BLEND_COVERAGE);
	int i;

	if (WARN_ON((supported_modes & ~valid_mode_mask) ||
		    ((supported_modes & BIT(DRM_MODE_BLEND_PREMULTI)) == 0)))
		return -EINVAL;

	prop = drm_property_create(dev, DRM_MODE_PROP_ENUM,
				   "pixel blend mode",
				   hweight32(supported_modes));
	if (!prop)
		return -ENOMEM;

	for (i = 0; i < ARRAY_SIZE(props); i++) {
		int ret;

		if (!(BIT(props[i].type) & supported_modes))
			continue;

		ret = drm_property_add_enum(prop, props[i].type,
					    props[i].name);

		if (ret) {
			drm_property_destroy(dev, prop);

			return ret;
		}
	}

	drm_object_attach_property(&plane->base, prop, DRM_MODE_BLEND_PREMULTI);
	plane->blend_mode_property = prop;

	return 0;
}
EXPORT_SYMBOL(drm_plane_create_blend_mode_property);

static int __drm_object_property_get_prop_value(struct drm_mode_object *obj,
						struct drm_property *property,
						uint64_t *val)
{
	int i;

	for (i = 0; i < obj->properties->count; i++) {
		if (obj->properties->properties[i] == property) {
			*val = obj->properties->values[i];
			return 0;
		}
	}

	return -EINVAL;
}
int drm_object_property_get_default_value(struct drm_mode_object *obj,
					  struct drm_property *property,
					  uint64_t *val)
{
	WARN_ON(!drm_drv_uses_atomic_modeset(property->dev));

	return __drm_object_property_get_prop_value(obj, property, val);
}
EXPORT_SYMBOL(drm_object_property_get_default_value);


void __drm_atomic_helper_plane_state_reset(struct drm_plane_state *plane_state,
					   struct drm_plane *plane)
{
	u64 val;

	plane_state->plane = plane;
	plane_state->rotation = DRM_MODE_ROTATE_0;

	plane_state->alpha = DRM_BLEND_ALPHA_OPAQUE;
	plane_state->pixel_blend_mode = DRM_MODE_BLEND_PREMULTI;

	if (plane->color_encoding_property) {
		if (!drm_object_property_get_default_value(&plane->base,
							   plane->color_encoding_property,
							   &val))
			plane_state->color_encoding = val;
	}

	if (plane->color_range_property) {
		if (!drm_object_property_get_default_value(&plane->base,
							   plane->color_range_property,
							   &val))
			plane_state->color_range = val;
	}

	if (plane->zpos_property) {
		if (!drm_object_property_get_default_value(&plane->base,
							   plane->zpos_property,
							   &val)) {
			plane_state->zpos = val;
			plane_state->normalized_zpos = val;
		}
	}
}
EXPORT_SYMBOL(__drm_atomic_helper_plane_state_reset);

/**
 * __drm_atomic_helper_plane_reset - reset state on plane
 * @plane: drm plane
 * @plane_state: plane state to assign
 *
 * Initializes the newly allocated @plane_state and assigns it to
 * the &drm_crtc->state pointer of @plane, usually required when
 * initializing the drivers or when called from the &drm_plane_funcs.reset
 * hook.
 *
 * This is useful for drivers that subclass the plane state.
 */
void __drm_atomic_helper_plane_reset(struct drm_plane *plane,
				     struct drm_plane_state *plane_state)
{
	if (plane_state)
		__drm_atomic_helper_plane_state_reset(plane_state, plane);

	plane->state = plane_state;
}
EXPORT_SYMBOL(__drm_atomic_helper_plane_reset);

// fb dev
#include <drm/drm_fb_helper.h>

/**
 * drm_fb_helper_fill_info - initializes fbdev information
 * @info: fbdev instance to set up
 * @fb_helper: fb helper instance to use as template
 * @sizes: describes fbdev size and scanout surface size
 *
 * Sets up the variable and fixed fbdev metainformation from the given fb helper
 * instance and the drm framebuffer allocated in &drm_fb_helper.fb.
 *
 * Drivers should call this (or their equivalent setup code) from their
 * &drm_fb_helper_funcs.fb_probe callback after having allocated the fbdev
 * backing storage framebuffer.
 */
void drm_fb_helper_fill_info(struct fb_info *info,
			     struct drm_fb_helper *fb_helper,
			     struct drm_fb_helper_surface_size *sizes)
{
	struct drm_framebuffer *fb = fb_helper->fb;

	drm_fb_helper_fill_fix(info, fb->pitches[0], fb->format->depth);
	drm_fb_helper_fill_var(info, fb_helper,
			       sizes->fb_width, sizes->fb_height);

	info->par = fb_helper;
	/*
	 * The DRM drivers fbdev emulation device name can be confusing if the
	 * driver name also has a "drm" suffix on it. Leading to names such as
	 * "simpledrmdrmfb" in /proc/fb. Unfortunately, it's an uAPI and can't
	 * be changed due user-space tools (e.g: pm-utils) matching against it.
	 */
	snprintf(info->fix.id, sizeof(info->fix.id), "%sdrmfb",
		 fb_helper->dev->driver->name);

}
EXPORT_SYMBOL(drm_fb_helper_fill_info);
