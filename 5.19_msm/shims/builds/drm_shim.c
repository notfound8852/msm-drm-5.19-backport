#ifndef DRM_WRITEBACK_CONNECTOR_INIT_WITH_ENCODER
#define DRM_WRITEBACK_CONNECTOR_INIT_WITH_ENCODER
#include <drm/drm_writeback.h>
#include <drm/drm_device.h>

/*
 * These are custom implementations for missing featuers on
 * downstream.
*/

int create_writeback_properties(struct drm_device *dev)
{
	struct drm_property *prop;

	if (!dev->mode_config.writeback_fb_id_property) {
		prop = drm_property_create_object(dev, DRM_MODE_PROP_ATOMIC,
						  "WRITEBACK_FB_ID",
						  DRM_MODE_OBJECT_FB);
		if (!prop)
			return -ENOMEM;
		dev->mode_config.writeback_fb_id_property = prop;
	}

	if (!dev->mode_config.writeback_pixel_formats_property) {
		prop = drm_property_create(dev, DRM_MODE_PROP_BLOB |
					   DRM_MODE_PROP_ATOMIC |
					   DRM_MODE_PROP_IMMUTABLE,
					   "WRITEBACK_PIXEL_FORMATS", 0);
		if (!prop)
			return -ENOMEM;
		dev->mode_config.writeback_pixel_formats_property = prop;
	}

	if (!dev->mode_config.writeback_out_fence_ptr_property) {
		prop = drm_property_create_range(dev, DRM_MODE_PROP_ATOMIC,
						 "WRITEBACK_OUT_FENCE_PTR", 0,
						 U64_MAX);
		if (!prop)
			return -ENOMEM;
		dev->mode_config.writeback_out_fence_ptr_property = prop;
	}

	return 0;
}

/*
 * This function is made for 4.19, will likely need to be changed
 * for newer versions.
*/
int drm_writeback_connector_init_with_encoder(
		struct drm_device *dev,
		struct drm_writeback_connector *wb_connector,
		struct drm_encoder *enc,
		const struct drm_connector_funcs *con_funcs,
		const u32 *formats, int n_formats)
{
	struct drm_property_blob *blob;
	struct drm_connector *connector = &wb_connector->base;
	struct drm_mode_config *config = &dev->mode_config;
	int ret = create_writeback_properties(dev);

	if (ret != 0)
		return ret;

	blob = drm_property_create_blob(dev, n_formats * sizeof(*formats), formats);
	if (IS_ERR(blob))
		return PTR_ERR(blob);

	connector->interlace_allowed = 0;

	ret = drm_connector_init(dev, connector, con_funcs,
							 DRM_MODE_CONNECTOR_WRITEBACK);
	if (ret)
		goto connector_fail;

	ret = drm_connector_attach_encoder(connector, enc);
	if (ret)
		goto attach_fail;

	connector->status = connector_status_connected;

	INIT_LIST_HEAD(&wb_connector->job_queue);
	spin_lock_init(&wb_connector->job_lock);
	wb_connector->fence_context = dma_fence_context_alloc(1);
	spin_lock_init(&wb_connector->fence_lock);
	snprintf(wb_connector->timeline_name,
			 sizeof(wb_connector->timeline_name),
			 "CONNECTOR:%d-%s", connector->base.id, connector->name);

	drm_object_attach_property(&connector->base,
							   config->writeback_out_fence_ptr_property, 0);
	drm_object_attach_property(&connector->base,
							   config->writeback_fb_id_property, 0);
	drm_object_attach_property(&connector->base,
							   config->writeback_pixel_formats_property,
							   blob->base.id);
	wb_connector->pixel_formats_blob_ptr = blob;
	return 0;

attach_fail:
	drm_connector_cleanup(connector);
connector_fail:
	drm_property_blob_put(blob);
	return ret;
}
EXPORT_SYMBOL(drm_writeback_connector_init_with_encoder);
#endif

#ifndef DRM_FIRMWARE_DRIVERS_ONLY
#define DRM_FIRMWARE_DRIVERS_ONLY
/*
 * drm_firmware_drivers_only() - Check if 'nomodeset' was passed at boot.
 *
 * This function doesn't exist natively on downstream 4.19, so this is our shim.
 * Instead of dealing with fbdev state parsing, we just look at the raw bootloader
 * cmdline directly to see if global mode-setting is disabled.
 */

extern char *saved_command_line;

bool drm_firmware_drivers_only(void)
{
	if (saved_command_line && strstr(saved_command_line, "nomodeset")) {
		return true;
	}

	return false;
}

EXPORT_SYMBOL(drm_firmware_drivers_only);
#endif
