/*
 * Copyright (C) 2014 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/backlight.h>
#include <linux/debugfs.h>
#include <linux/gpio.h>
#include <linux/module.h>
#include <linux/of_gpio.h>
#include <linux/gpio/machine.h>
#include <linux/regulator/consumer.h>

#include <drm/drmP.h>
#include <drm/drm_crtc.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_panel.h>

#include <drm/panel/panel-jdi-lpm102a188a.h>

#include <video/mipi_display.h>

struct panel_jdi {
	struct drm_panel base;
	struct mipi_dsi_device *link1;
	struct mipi_dsi_device *link2;

	struct regulator *supply;
	struct regulator *ddi_supply;
	struct backlight_device *backlight;
	int enable_gpio;
	unsigned long enable_gpio_flags;
	int reset_gpio;
	unsigned long reset_gpio_flags;

	const struct drm_display_mode *mode;

	bool prepared;
	bool enabled;

	struct dentry *debugfs_entry;
	u8 current_register;
};

static inline struct panel_jdi *to_panel_jdi(struct drm_panel *panel)
{
	return container_of(panel, struct panel_jdi, base);
}

static void jdi_wait_frames(struct panel_jdi *jdi, unsigned int frames)
{
	unsigned int refresh = drm_mode_vrefresh(jdi->mode);

	if (WARN_ON(frames > refresh))
		return;

	msleep(1000 / (refresh / frames));
}

static int panel_jdi_write_display_brightness(struct panel_jdi *jdi)
{
	int ret;
	u8 data;

	data = RSP_WRITE_DISPLAY_BRIGHTNESS(0xFF);

	ret = mipi_dsi_dcs_write(jdi->link1,
			MIPI_DCS_RSP_WRITE_DISPLAY_BRIGHTNESS, &data, 1);
	if (ret < 1) {
		DRM_INFO("failed to write display brightness: %d\n", ret);
		return ret;
	}

	ret = mipi_dsi_dcs_write(jdi->link2,
			MIPI_DCS_RSP_WRITE_DISPLAY_BRIGHTNESS, &data, 1);
	if (ret < 1) {
		DRM_INFO("failed to write display brightness: %d\n", ret);
		return ret;
	}

	return 0;
}

static int panel_jdi_write_control_display(struct panel_jdi *jdi)
{
	int ret;
	u8 data;

	data = RSP_WRITE_CONTROL_DISPLAY_BL_ON |
			RSP_WRITE_CONTROL_DISPLAY_BCTRL_LEDPWM;

	ret = mipi_dsi_dcs_write(jdi->link1, MIPI_DCS_RSP_WRITE_CONTROL_DISPLAY,
			&data, 1);
	if (ret < 1) {
		DRM_INFO("failed to write control display: %d\n", ret);
		return ret;
	}

	ret = mipi_dsi_dcs_write(jdi->link2, MIPI_DCS_RSP_WRITE_CONTROL_DISPLAY,
			&data, 1);
	if (ret < 1) {
		DRM_INFO("failed to write control display: %d\n", ret);
		return ret;
	}

	return 0;
}

static int panel_jdi_write_adaptive_brightness_control(struct panel_jdi *jdi)
{
	int ret;
	u8 data;

	data = RSP_WRITE_ADAPTIVE_BRIGHTNESS_CONTROL_C_VIDEO;

	ret = mipi_dsi_dcs_write(jdi->link1,
			MIPI_DCS_RSP_WRITE_ADAPTIVE_BRIGHTNESS_CONTROL, &data,
			1);
	if (ret < 1) {
		DRM_INFO("failed to set adaptive brightness ctrl: %d\n", ret);
		return ret;
	}

	ret = mipi_dsi_dcs_write(jdi->link2,
			MIPI_DCS_RSP_WRITE_ADAPTIVE_BRIGHTNESS_CONTROL, &data,
			1);
	if (ret < 1) {
		DRM_INFO("failed to set adaptive brightness ctrl: %d\n", ret);
		return ret;
	}

	return 0;
}

static int panel_jdi_disable(struct drm_panel *panel)
{
	struct panel_jdi *jdi = to_panel_jdi(panel);

	if (!jdi->enabled)
		return 0;

	backlight_disable(jdi->backlight);

	jdi->enabled = false;

	return 0;
}

static int panel_jdi_unprepare(struct drm_panel *panel)
{
	struct panel_jdi *jdi = to_panel_jdi(panel);
	int ret;

	if (!jdi->prepared)
		return 0;

	jdi_wait_frames(jdi, 2);

	ret = mipi_dsi_dcs_set_display_off(jdi->link1);
	if (ret < 0)
		DRM_INFO("failed to set display off: %d\n", ret);
	ret = mipi_dsi_dcs_set_display_off(jdi->link2);
	if (ret < 0)
		DRM_INFO("failed to set display off: %d\n", ret);

	/* Specified by JDI @ 50ms, subject to change */
	msleep(50);

	ret = mipi_dsi_dcs_enter_sleep_mode(jdi->link1);
	if (ret < 0)
		DRM_INFO("failed to enter sleep mode: %d\n", ret);
	ret = mipi_dsi_dcs_enter_sleep_mode(jdi->link2);
	if (ret < 0)
		DRM_INFO("failed to enter sleep mode: %d\n", ret);

	/* Specified by JDI @ 150ms, subject to change */
	msleep(150);

	gpio_set_value(jdi->reset_gpio,
		(jdi->reset_gpio_flags & GPIO_ACTIVE_LOW) ? 0 : 1);

	/* T4 = 1ms */
	usleep_range(1000, 3000);

	gpio_set_value(jdi->enable_gpio,
		(jdi->enable_gpio_flags & GPIO_ACTIVE_LOW) ? 1 : 0);

	/* T5 = 2ms */
	usleep_range(2000, 4000);

	regulator_disable(jdi->ddi_supply);

	/* T6 = 2ms */
	usleep_range(5000, 6000);

	regulator_disable(jdi->supply);

	jdi->prepared = false;

	return 0;
}

static int jdi_setup_symmetrical_split(struct mipi_dsi_device *left,
					 struct mipi_dsi_device *right,
					 const struct drm_display_mode *mode)
{
	int err;

	err = mipi_dsi_dcs_set_column_address(left, 0, mode->hdisplay / 2 - 1);
	if (err < 0) {
		dev_err(&left->dev, "failed to set column address: %d\n", err);
		return err;
	}


	err = mipi_dsi_dcs_set_column_address(right, 0, mode->hdisplay / 2 - 1);
	if (err < 0) {
		dev_err(&right->dev, "failed to set column address: %d\n", err);
		return err;
	}

	err = mipi_dsi_dcs_set_page_address(left, 0, mode->vdisplay - 1);
	if (err < 0) {
		dev_err(&left->dev, "failed to set page address: %d\n", err);
		return err;
	}

	err = mipi_dsi_dcs_set_page_address(right, 0, mode->vdisplay - 1);
	if (err < 0) {
		dev_err(&right->dev, "failed to set page address: %d\n", err);
		return err;
	}

	return 0;
}

static int panel_jdi_prepare(struct drm_panel *panel)
{
	struct panel_jdi *jdi = to_panel_jdi(panel);
	u8 format = MIPI_DCS_PIXEL_FMT_24BIT;
	int err;

	if (jdi->prepared)
		return 0;


	if (!jdi->enabled) {
		gpio_set_value(jdi->enable_gpio,
			(jdi->enable_gpio_flags & GPIO_ACTIVE_LOW) ? 0 : 1);

		/* T3 = 10ms */
		usleep_range(10000, 15000);

		gpio_set_value(jdi->reset_gpio,
			(jdi->reset_gpio_flags & GPIO_ACTIVE_LOW) ? 1 : 0);

		/* Specified by JDI @ 3ms, subject to change */
		usleep_range(3000, 5000);
	}

	err = regulator_enable(jdi->supply);
	if (err < 0) {
		DRM_INFO("failed to enable supply: %d\n", err);
		return err;
	}

	/* T1 = 2ms */
	usleep_range(2000, 4000);

	err = regulator_enable(jdi->ddi_supply);
	if (err < 0) {
		DRM_INFO("failed to enable ddi_supply: %d\n", err);
		return err;
	}

	/* T2 = 1ms */
	usleep_range(1000, 3000);

	/*
	 * TODO: The device supports both left-right and even-odd split
	 * configurations, but this driver currently supports only the left-
	 * right split. To support a different mode a mechanism needs to be
	 * put in place to communicate the configuration back to the DSI host
	 * controller.
	 */
	err = jdi_setup_symmetrical_split(jdi->link1, jdi->link2,
					    jdi->mode);
	if (err < 0) {
		dev_err(panel->dev, "failed to set up symmetrical split: %d\n",
			err);
		goto poweroff;
	}

	err = mipi_dsi_dcs_exit_sleep_mode(jdi->link1);
	if (err < 0) {
		dev_err(panel->dev, "failed to exit sleep mode: %d\n", err);
		goto poweroff;
	}

	err = mipi_dsi_dcs_exit_sleep_mode(jdi->link2);
	if (err < 0) {
		dev_err(panel->dev, "failed to exit sleep mode: %d\n", err);
		goto poweroff;
	}
	msleep(5); 

	err = mipi_dsi_dcs_set_tear_scanline(jdi->link1,
					     jdi->mode->vdisplay - 16);
	if (err < 0) {
		DRM_ERROR("failed to set tear scanline: %d\n", err);
		goto poweroff;
	}

	err = mipi_dsi_dcs_set_tear_scanline(jdi->link2,
					     jdi->mode->vdisplay - 16);
	if (err < 0) {
		DRM_ERROR("failed to set tear scanline: %d\n", err);
		goto poweroff;
	}

	err = mipi_dsi_dcs_set_tear_on(jdi->link1, MIPI_DSI_DCS_TEAR_MODE_VBLANK);
	if (err < 0) {
		dev_err(panel->dev, "failed to set tear on: %d\n", err);
		goto poweroff;
	}

	err = mipi_dsi_dcs_set_tear_on(jdi->link2, MIPI_DSI_DCS_TEAR_MODE_VBLANK);
	if (err < 0) {
		dev_err(panel->dev, "failed to set tear on: %d\n", err);
		goto poweroff;
	}

	err = mipi_dsi_dcs_set_address_mode(jdi->link1, false, false, false,
			false, false, false, false, false);
	if (err < 0) {
		dev_err(panel->dev, "failed to set address mode: %d\n", err);
		goto poweroff;
	}

	err = mipi_dsi_dcs_set_address_mode(jdi->link2, false, false,
			false, false, false, false, false, false);
	if (err < 0) {
		dev_err(panel->dev, "failed to set address mode: %d\n", err);
		goto poweroff;
	}

	err = mipi_dsi_dcs_set_pixel_format(jdi->link1, format);
	if (err < 0) {
		dev_err(panel->dev, "failed to set pixel format: %d\n", err);
		goto poweroff;
	}

	err = mipi_dsi_dcs_set_pixel_format(jdi->link2, format);
	if (err < 0) {
		dev_err(panel->dev, "failed to set pixel format: %d\n", err);
		goto poweroff;
	}

	err = panel_jdi_write_display_brightness(jdi);
	if (err < 0) {
		dev_err(panel->dev, "failed to write display brightness: %d\n", err);
		goto poweroff;
	}

	err = panel_jdi_write_control_display(jdi);
	if (err < 0) {
		dev_err(panel->dev, "failed to write control display: %d\n", err);
		goto poweroff;
	}

	err = panel_jdi_write_adaptive_brightness_control(jdi);
	if (err < 0) {
		dev_err(panel->dev, "failed to set adaptive brightness ctrl: %d\n", err);
		goto poweroff;
	}

	/*
	 * We need to wait 150ms between mipi_dsi_dcs_exit_sleep_mode() and
	 * mipi_dsi_dcs_set_display_on().
	 */
	msleep(150);

	/*
	 * Unless we send one frame of image data before display turn on, the
	 * display may show random pixels (colored snow).
	 */

	err = mipi_dsi_dcs_set_display_on(jdi->link1);
	if (err < 0) {
		dev_err(panel->dev, "failed to set display on: %d\n", err);
		goto poweroff;
	}

	err = mipi_dsi_dcs_set_display_on(jdi->link2);
	if (err < 0) {
		dev_err(panel->dev, "failed to set display on: %d\n", err);
		goto poweroff;
	}

	jdi->prepared = true;

	/* wait for 6 frames before continuing */
	jdi_wait_frames(jdi, 6);

	return 0;

poweroff:
	regulator_disable(jdi->ddi_supply);

	/* T6 = 2ms */
	usleep_range(7000, 9000);

	regulator_disable(jdi->supply);

	return err;
}

static int panel_jdi_enable(struct drm_panel *panel)
{
	struct panel_jdi *jdi = to_panel_jdi(panel);

	if (jdi->enabled)
		return 0;

	backlight_enable(jdi->backlight);

	jdi->enabled = true;

	return 0;
}

static const struct drm_display_mode default_mode = {
	.clock = 331334,
	.hdisplay = 2560,
	.hsync_start = 2560 + 80,
	.hsync_end = 2560 + 80 + 80,
	.htotal = 2560 + 80 + 80 + 80,
	.vdisplay = 1800,
	.vsync_start = 1800 + 4,
	.vsync_end = 1800 + 4 + 4,
	.vtotal = 1800 + 4 + 4 + 4,

	.vrefresh = 60,
};

static int panel_jdi_get_modes(struct drm_panel *panel)
{
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(panel->drm, &default_mode);
	if (!mode) {
		DRM_INFO("failed to add mode %ux%ux@%u\n",
			default_mode.hdisplay, default_mode.vdisplay,
			default_mode.vrefresh);
		return -ENOMEM;
	}

	drm_mode_set_name(mode);

	drm_mode_probed_add(panel->connector, mode);

	panel->connector->display_info.width_mm = 211;
	panel->connector->display_info.height_mm = 148;
	panel->connector->display_info.bpc = 8;

	return 1;
}

static int jdi_register_get(void *data, u64 *val)
{
	struct panel_jdi *jdi = (struct panel_jdi*) data;

	*val = jdi->current_register;

	return 0;
}

static int jdi_register_set(void *data, u64 val)
{
	struct panel_jdi *jdi = (struct panel_jdi*) data;

	jdi->current_register = val;

	return 0;
}

static int jdi_value_get(void *data, u64 *val)
{
	struct panel_jdi *jdi = (struct panel_jdi*) data;
	int ret;
	u8 value = 0;

	ret = mipi_dsi_dcs_read(jdi->link1, jdi->current_register, &value, 1);
	if (ret < 1) {
		DRM_INFO("failed to write control display: %d\n", ret);
		return ret;
	}

	*val = value;

	return 0;
}

static int jdi_value_set(void *data, u64 val)
{
	struct panel_jdi *jdi = (struct panel_jdi*) data;
	int ret;
	u8 value = val;

	ret = mipi_dsi_dcs_write(jdi->link1, jdi->current_register, &value, 1);
	if (ret < 1) {
		DRM_INFO("failed to write control display: %d\n", ret);
		return ret;
	}

	ret = mipi_dsi_dcs_write(jdi->link2, jdi->current_register, &value, 1);
	if (ret < 1) {
		DRM_INFO("failed to write control display: %d\n", ret);
		return ret;
	}

	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(jdi_register_fops,
			jdi_register_get, jdi_register_set,
			"%llu\n");

DEFINE_SIMPLE_ATTRIBUTE(jdi_value_fops,
			jdi_value_get, jdi_value_set,
			"%llu\n");

static int panel_jdi_debugfs_init(struct panel_jdi *jdi)
{
	jdi->debugfs_entry = debugfs_create_dir("jdi-lpm102a188a", NULL);

	debugfs_create_file("register", S_IWUGO | S_IRUGO, jdi->debugfs_entry,
				jdi, &jdi_register_fops);

	debugfs_create_file("value", S_IWUGO | S_IRUGO, jdi->debugfs_entry,
				jdi, &jdi_value_fops);

	return 0;
}

static void panel_jdi_debugfs_cleanup(struct panel_jdi *jdi)
{
	debugfs_remove_recursive(jdi->debugfs_entry);
}

static const struct drm_panel_funcs panel_jdi_funcs = {
	.prepare = panel_jdi_prepare,
	.enable = panel_jdi_enable,
	.disable = panel_jdi_disable,
	.unprepare = panel_jdi_unprepare,
	.get_modes = panel_jdi_get_modes,
};

static const struct of_device_id jdi_of_match[] = {
	{ .compatible = "jdi,lpm102a188a", },
	{ }
};
MODULE_DEVICE_TABLE(of, jdi_of_match);

static int jdi_panel_add(struct panel_jdi *jdi)
{
	struct device_node *np;
	enum of_gpio_flags gpio_flags;
	int err = 0;
	unsigned int value;

	jdi->mode = &default_mode;

	jdi->supply = devm_regulator_get(&jdi->link1->dev, "power");
	if (IS_ERR(jdi->supply))
		return PTR_ERR(jdi->supply);

	jdi->ddi_supply = devm_regulator_get(&jdi->link1->dev, "ddi");
	if (IS_ERR(jdi->ddi_supply))
		return PTR_ERR(jdi->ddi_supply);

	np = of_parse_phandle(jdi->link1->dev.of_node, "backlight", 0);
	if (np) {
		jdi->backlight = of_find_backlight_by_node(np);
		of_node_put(np);

		if (!jdi->backlight)
			return -EPROBE_DEFER;
	}

	jdi->enable_gpio = of_get_named_gpio_flags(jdi->link1->dev.of_node,
				"enable-gpio", 0, &gpio_flags);
	if (!gpio_is_valid(jdi->enable_gpio)) {
		DRM_INFO("enable gpio not found: %d\n", err);
		return -ENODEV;
	}

	if (gpio_flags & OF_GPIO_ACTIVE_LOW)
		jdi->enable_gpio_flags |= GPIO_ACTIVE_LOW;

	err = devm_gpio_request(&jdi->link1->dev, jdi->enable_gpio, "jdi-enable");
	if (err < 0) {
		DRM_INFO("Request enable gpio failed: %d\n", err);
		return err;
	}

	value = (jdi->enable_gpio_flags & GPIO_ACTIVE_LOW) ? 0 : 1;
	err = gpio_direction_output(jdi->enable_gpio, value);
	if (err < 0) {
		DRM_INFO("Set enable gpio direction failed: %d\n", err);
		return err;
	}

	jdi->reset_gpio = of_get_named_gpio_flags(jdi->link1->dev.of_node,
				"reset-gpio", 0, &gpio_flags);
	if (!gpio_is_valid(jdi->reset_gpio)) {
		DRM_INFO("reset gpio not found: %d\n", err);
		return -ENODEV;
	}

	if (gpio_flags & OF_GPIO_ACTIVE_LOW)
		jdi->reset_gpio_flags |= GPIO_ACTIVE_LOW;

	err = devm_gpio_request(&jdi->link1->dev, jdi->reset_gpio, "jdi-reset");
	if (err < 0) {
		DRM_INFO("Request reset gpio failed: %d\n", err);
		return err;
	}

	value = (jdi->reset_gpio_flags & GPIO_ACTIVE_LOW) ? 1 : 0;
	err = gpio_direction_output(jdi->reset_gpio, value);
	if (err < 0) {
		DRM_INFO("Set enable gpio direction failed: %d\n", err);
		return err;
	}

	drm_panel_init(&jdi->base);
	jdi->base.dev = &jdi->link1->dev;
	jdi->base.funcs = &panel_jdi_funcs;

	err = drm_panel_add(&jdi->base);
	if (err < 0) {
		DRM_INFO("drm_panel_add failed: %d\n", err);
		goto put_backlight;
	}

	panel_jdi_debugfs_init(jdi);

	return 0;

put_backlight:
	if (jdi->backlight)
		put_device(&jdi->backlight->dev);

	return err;
}

static void jdi_panel_del(struct panel_jdi *jdi)
{
	if (jdi->base.dev)
		drm_panel_remove(&jdi->base);

	if (jdi->backlight)
		put_device(&jdi->backlight->dev);

	if (jdi->link2)
		put_device(&jdi->link2->dev);
}

static int panel_jdi_dsi_probe(struct mipi_dsi_device *dsi)
{
	struct mipi_dsi_device *secondary = NULL;
	struct panel_jdi *jdi;
	struct device_node *np;
	int err;

	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_LPM;

	/* Find DSI-LINK1 */
	np = of_parse_phandle(dsi->dev.of_node, "link2", 0);
	if (np) {
		secondary = of_find_mipi_dsi_device_by_node(np);
		of_node_put(np);

		if (!secondary)
			return -EPROBE_DEFER;
	}

	/* register a panel for only the DSI-LINK1 interface */
	if (secondary) {
		jdi = devm_kzalloc(&dsi->dev, sizeof(*jdi), GFP_KERNEL);
		if (!jdi) {
			put_device(&secondary->dev);
			return -ENOMEM;
		}

		mipi_dsi_set_drvdata(dsi, jdi);

		jdi->link2 = secondary;
		jdi->link1 = dsi;

		err = jdi_panel_add(jdi);
		if (err < 0) {
			put_device(&secondary->dev);
			return err;
		}
	}

	err = mipi_dsi_attach(dsi);
	if (err < 0) {
		if (secondary)
			jdi_panel_del(jdi);

		return err;
	}

	return 0;
}

static int panel_jdi_dsi_remove(struct mipi_dsi_device *dsi)
{
	struct panel_jdi *jdi = mipi_dsi_get_drvdata(dsi);
	int err;

	/* only detach from host for the DSI-LINK2 interface */
	if (!jdi) {
		mipi_dsi_detach(dsi);
		return 0;
	}

	err = panel_jdi_disable(&jdi->base);
	if (err < 0)
		dev_err(&dsi->dev, "failed to disable panel: %d\n", err);

	err = mipi_dsi_detach(dsi);
	if (err < 0)
		dev_err(&dsi->dev, "failed to detach from DSI host: %d\n", err);

	drm_panel_detach(&jdi->base);
	jdi_panel_del(jdi);

	return 0;
}

static void panel_jdi_dsi_shutdown(struct mipi_dsi_device *dsi)
{
	struct panel_jdi *jdi = mipi_dsi_get_drvdata(dsi);

	if (!jdi)
		return;

	panel_jdi_debugfs_cleanup(jdi);
	panel_jdi_disable(&jdi->base);
}

static struct mipi_dsi_driver panel_jdi_dsi_driver = {
	.driver = {
		.name = "panel-jdi-lpm102a188a-dsi",
		.of_match_table = jdi_of_match,
	},
	.probe = panel_jdi_dsi_probe,
	.remove = panel_jdi_dsi_remove,
	.shutdown = panel_jdi_dsi_shutdown,
};
module_mipi_dsi_driver(panel_jdi_dsi_driver);
MODULE_AUTHOR("Sean Paul <seanpaul@chromium.org>");
MODULE_DESCRIPTION("DRM Driver for JDI LPM102A188A");
MODULE_LICENSE("GPL and additional rights");
