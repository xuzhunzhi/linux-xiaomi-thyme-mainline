// SPDX-License-Identifier: GPL-2.0-only
/*
 * Author: Jesper2333 | https://github.com/Jesper2333 | @Jesper2333 | jesper@hyjon.cn
 *
 * DRM panel driver for the Xiaomi thyme (Mi 10S, SM8250) Samsung
 * "42 02 0b" DSI command-mode panel (J1U series, DSC 1.1).
 *
 * Ported from the stock 4.19 vendor DTSI
 * (dsi-panel-j1u-42-02-0b-dsc-cmd.dtsi):
 *   - 1080x2340, DSI cmd mode, 4 lanes, burst mode
 *   - DSC 1.1: slice 12x540, 2 slices/pkt, 8 bpc, 8 bpp, block-pred
 *   - 60 Hz / 90 Hz modes
 *   - supplies: vddio 1.8 V, vci 3.0 V; reset GPIO active-low; TE pin
 *   - backlight: DCS 0x51, 11-bit (1..2047)
 *
 * Targets linux-7.0: mipi_dsi_multi_context accumulated-error API,
 * devm_drm_panel_alloc(); functionally equivalent to the 6.6 port in
 * mainline_kernel/panel-42020b/.
 */

#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regulator/consumer.h>

#include <drm/drm_mipi_dsi.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>
#include <drm/display/drm_dsc.h>
#include <drm/display/drm_dsc_helper.h>

#include <video/mipi_display.h>

struct thyme_42020b {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	struct gpio_desc *reset_gpio;
	struct regulator_bulk_data supplies[2];
	int vrefresh;
	bool prepared;
	bool enabled;
};

static inline struct thyme_42020b *to_thyme_42020b(struct drm_panel *panel)
{
	return container_of(panel, struct thyme_42020b, panel);
}

static void thyme_42020b_reset(struct thyme_42020b *ctx)
{
	/* stock reset sequence: 1,1 / 0,1 / 1,10 (ms) */
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	usleep_range(1000, 2000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(1000, 2000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	usleep_range(10000, 11000);
}

/*
 * Stock 60/90 Hz common init (on-command minus the refresh-rate entry
 * and sleep-out/display-on, which are emitted separately).
 */
static int thyme_42020b_init(struct thyme_42020b *ctx)
{
	struct mipi_dsi_device *dsi = ctx->dsi;
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = dsi };

	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	/* Set page 7 - Round OFF */
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xF0, 0x55, 0xAA, 0x52, 0x08, 0x07);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xC0, 0x10);
	/* Set page 0 - AOD Demura power off */
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xF0, 0x55, 0xAA, 0x52, 0x08, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6F, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xC0, 0x33);
	/* Backlight dimming 8 frame */
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xB2, 0x58, 0x00, 0x08, 0x08);
	/* Set page 4 - Demura */
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xF0, 0x55, 0xAA, 0x52, 0x08, 0x04);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xC2, 0x14);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xB1, 0x01, 0xC1);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xD0, 0x00, 0x80, 0x00, 0x00, 0x80); /* Demura ON */
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6F, 0x09);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xB1, 0xF0, 0x47, 0x13);
	/* Dcode */
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xFF, 0xAA, 0x55, 0xA5, 0x81);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6F, 0x0D);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xF3, 0xAB);
	/* 101.6M OSC */
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xFF, 0xAA, 0x55, 0xA5, 0x81);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6F, 0x05);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xFD, 0x00, 0xDA);
	/* CMD3 lpm_dec_all_on */
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xFF, 0xAA, 0x55, 0xA5, 0x80);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6F, 0x0A);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xFC, 0x02);
	/* Page disable */
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xF0, 0x55, 0xAA, 0x52, 0x00, 0x00);
	/* VBP/VFP video mode */
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x3B, 0x00, 0x0C, 0x00, 0x04);
	/* COMPRESSION_METHOD = 1 VESA, slice 12 */
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x90, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x93, 0x89, 0x28, 0x00, 0x0C, 0x02,
				     0x00, 0x02, 0x0E, 0x01, 0x1F, 0x00, 0x07,
				     0x08, 0xBB, 0x08, 0x7A, 0x10, 0xF0);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x03, 0x11);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x2C, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x35, 0x00); /* TE ON */
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x53, 0x20);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x51, 0x00, 0x00, 0x00, 0x00); /* normal mode */
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x2A, 0x00, 0x00, 0x04, 0x37);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x2B, 0x00, 0x00, 0x09, 0x23);
	/* ESD config */
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xFF, 0xAA, 0x55, 0xA5, 0x80);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6F, 0x36);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xF6, 0x42);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xF0, 0x55, 0xAA, 0x52, 0x08, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6F, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xBE, 0x0B);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6F, 0x05);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xBE, 0x8A);

	return dsi_ctx.accum_err;
}

static int thyme_42020b_set_refresh_rate(struct thyme_42020b *ctx, int vrefresh)
{
	struct mipi_dsi_device *dsi = ctx->dsi;
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = dsi };

	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;
	if (vrefresh >= 90) {
		/* Enter 90Hz 1 pulse */
		mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x2F, 0x04);
		mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x26, 0x02);
	} else {
		/* Enter 60Hz 1 pulse */
		mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x2F, 0x01);
		mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x26, 0x01);
	}
	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	return dsi_ctx.accum_err;
}

static int thyme_42020b_on(struct thyme_42020b *ctx)
{
	int ret;

	ret = thyme_42020b_init(ctx);
	if (ret < 0)
		return ret;

	return thyme_42020b_set_refresh_rate(ctx, ctx->vrefresh);
}

static int thyme_42020b_prepare(struct drm_panel *panel)
{
	struct thyme_42020b *ctx = to_thyme_42020b(panel);
	int ret;

	if (ctx->prepared)
		return 0;

	ret = regulator_bulk_enable(ARRAY_SIZE(ctx->supplies), ctx->supplies);
	if (ret < 0)
		return ret;

	thyme_42020b_reset(ctx);

	ctx->prepared = true;
	return 0;
}

static int thyme_42020b_enable(struct drm_panel *panel)
{
	struct thyme_42020b *ctx = to_thyme_42020b(panel);
	struct mipi_dsi_device *dsi = ctx->dsi;
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = dsi };
	int ret;

	if (ctx->enabled)
		return 0;

	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	/* default refresh is 60 Hz; DPU picks the mode, panel init is
	 * common for both timings and the 2F/26 pair selects the rate */
	ctx->vrefresh = 60;
	ret = thyme_42020b_on(ctx);
	if (ret < 0)
		dev_err(&dsi->dev, "failed to initialize panel: %d\n", ret);

	/* Sleep out (120 ms) then display on (20 ms) */
	mipi_dsi_dcs_exit_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 120);
	mipi_dsi_dcs_set_display_on_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 20);
	if (dsi_ctx.accum_err)
		dev_err(&dsi->dev, "failed to power on display: %d\n",
			dsi_ctx.accum_err);

	ctx->enabled = true;
	return 0;
}

static int thyme_42020b_disable(struct drm_panel *panel)
{
	struct thyme_42020b *ctx = to_thyme_42020b(panel);
	struct mipi_dsi_device *dsi = ctx->dsi;
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = dsi };

	if (!ctx->enabled)
		return 0;

	mipi_dsi_dcs_set_display_off_multi(&dsi_ctx);
	if (dsi_ctx.accum_err)
		dev_err(&dsi->dev, "failed to set display off: %d\n",
			dsi_ctx.accum_err);

	msleep(20);
	ctx->enabled = false;
	return 0;
}

static int thyme_42020b_unprepare(struct drm_panel *panel)
{
	struct thyme_42020b *ctx = to_thyme_42020b(panel);
	struct mipi_dsi_device *dsi = ctx->dsi;
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = dsi };

	if (!ctx->prepared)
		return 0;

	mipi_dsi_dcs_enter_sleep_mode_multi(&dsi_ctx);
	if (dsi_ctx.accum_err)
		dev_err(&dsi->dev, "failed to enter sleep mode: %d\n",
			dsi_ctx.accum_err);
	msleep(120);

	gpiod_set_value_cansleep(ctx->reset_gpio, 0);

	regulator_bulk_disable(ARRAY_SIZE(ctx->supplies), ctx->supplies);

	ctx->prepared = false;
	return 0;
}

/* 60 Hz: hfp16/hbp8/hpw8, vbp560/vfp600/vpw32 */
static const struct drm_display_mode thyme_42020b_mode_60 = {
	.clock = 1112 * 3532 * 60 / 1000,
	.hdisplay = 1080,
	.hsync_start = 1080 + 16,
	.hsync_end = 1080 + 16 + 8,
	.htotal = 1080 + 16 + 8 + 8,
	.vdisplay = 2340,
	.vsync_start = 2340 + 560,
	.vsync_end = 2340 + 560 + 32,
	.vtotal = 2340 + 560 + 32 + 600,
	.flags = DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC,
	.width_mm = 710,
	.height_mm = 1537,
};

/* 90 Hz: hfp16/hbp8/hpw8, vbp8/vfp4/vpw4 */
static const struct drm_display_mode thyme_42020b_mode_90 = {
	.clock = 1112 * 2356 * 90 / 1000,
	.hdisplay = 1080,
	.hsync_start = 1080 + 16,
	.hsync_end = 1080 + 16 + 8,
	.htotal = 1080 + 16 + 8 + 8,
	.vdisplay = 2340,
	.vsync_start = 2340 + 8,
	.vsync_end = 2340 + 8 + 4,
	.vtotal = 2340 + 8 + 4 + 4,
	.flags = DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC,
	.width_mm = 710,
	.height_mm = 1537,
};

static int thyme_42020b_get_modes(struct drm_panel *panel,
				  struct drm_connector *connector)
{
	struct drm_display_mode *mode;

	/* 60 Hz */
	mode = drm_mode_duplicate(connector->dev, &thyme_42020b_mode_60);
	if (!mode)
		return 0;
	drm_mode_set_name(mode);
	drm_mode_probed_add(connector, mode);

	/* 90 Hz */
	mode = drm_mode_duplicate(connector->dev, &thyme_42020b_mode_90);
	if (!mode)
		return 1;
	drm_mode_set_name(mode);
	drm_mode_probed_add(connector, mode);

	connector->display_info.width_mm = 71;
	connector->display_info.height_mm = 154;

	return 2;
}

static const struct drm_panel_funcs thyme_42020b_funcs = {
	.disable = thyme_42020b_disable,
	.unprepare = thyme_42020b_unprepare,
	.prepare = thyme_42020b_prepare,
	.enable = thyme_42020b_enable,
	.get_modes = thyme_42020b_get_modes,
};

static int thyme_42020b_bl_update_status(struct backlight_device *bl)
{
	struct mipi_dsi_device *dsi = bl_get_data(bl);
	u16 brightness = backlight_get_brightness(bl);

	/* DCS 0x51, 11-bit big-endian: 0x51 <hi7> <lo4> */
	return mipi_dsi_dcs_write(dsi, 0x51,
				  (u8[]){ (brightness >> 4) & 0x7f,
					  (brightness & 0x0f) << 4 }, 2);
}

static int thyme_42020b_bl_get_brightness(struct backlight_device *bl)
{
	return backlight_get_brightness(bl);
}

static const struct backlight_ops thyme_42020b_bl_ops = {
	.update_status = thyme_42020b_bl_update_status,
	.get_brightness = thyme_42020b_bl_get_brightness,
};

static struct backlight_device *
thyme_42020b_create_backlight(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	const struct backlight_properties props = {
		.type = BACKLIGHT_RAW,
		.brightness = 536,
		.max_brightness = 2047,
	};

	return devm_backlight_device_register(dev, dev_name(dev), dev, dsi,
					      &thyme_42020b_bl_ops, &props);
}

static int thyme_42020b_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct thyme_42020b *ctx;
	struct drm_dsc_config *dsc;
	int ret;

	ctx = devm_drm_panel_alloc(dev, struct thyme_42020b, panel,
				   &thyme_42020b_funcs,
				   DRM_MODE_CONNECTOR_DSI);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	dsc = devm_kzalloc(dev, sizeof(*dsc), GFP_KERNEL);
	if (!dsc)
		return -ENOMEM;

	/* DSC 1.1: slice 12x540, 2 slices/pkt, 8 bpc, 8 bpp, block-pred */
	dsc->dsc_version_major = 0x1;
	dsc->dsc_version_minor = 0x1;
	dsc->slice_height = 12;
	dsc->slice_width = 540;
	dsc->slice_count = 2;
	dsc->bits_per_component = 8;
	dsc->bits_per_pixel = 8 << 4;
	dsc->block_pred_enable = true;

	dsi->dsc = dsc;

	ctx->supplies[0].supply = "vddio";
	ctx->supplies[1].supply = "vci";

	ret = devm_regulator_bulk_get(dev, ARRAY_SIZE(ctx->supplies), ctx->supplies);
	if (ret < 0)
		return ret;

	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(ctx->reset_gpio),
				     "failed to get reset-gpios\n");

	ctx->dsi = dsi;
	mipi_dsi_set_drvdata(dsi, ctx);

	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB101010;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO_BURST |
			  MIPI_DSI_CLOCK_NON_CONTINUOUS | MIPI_DSI_MODE_LPM;


	/* stock lp11-init: DSI host must be up (LP-11) before panel prepare */
	ctx->panel.prepare_prev_first = true;

	ctx->panel.backlight = thyme_42020b_create_backlight(dsi);
	if (IS_ERR(ctx->panel.backlight))
		return dev_err_probe(dev, PTR_ERR(ctx->panel.backlight),
				     "failed to create backlight\n");

	drm_panel_add(&ctx->panel);

	ret = mipi_dsi_attach(dsi);
	if (ret < 0) {
		dev_err(dev, "failed to attach to DSI host: %d\n", ret);
		drm_panel_remove(&ctx->panel);
	}

	return ret;
}

static void thyme_42020b_remove(struct mipi_dsi_device *dsi)
{
	struct thyme_42020b *ctx = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "failed to detach DSI host: %d\n", ret);

	drm_panel_remove(&ctx->panel);
}

static const struct of_device_id thyme_42020b_of_match[] = {
	{ .compatible = "xiaomi,thyme-42-02-0b" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, thyme_42020b_of_match);

static struct mipi_dsi_driver thyme_42020b_driver = {
	.probe = thyme_42020b_probe,
	.remove = thyme_42020b_remove,
	.driver = {
		.name = "panel-xiaomi-thyme-42020b",
		.of_match_table = thyme_42020b_of_match,
	},
};
module_mipi_dsi_driver(thyme_42020b_driver);

MODULE_AUTHOR("Jesper2333 <jesper@hyjon.cn>");
MODULE_DESCRIPTION("DRM panel driver for Xiaomi thyme (SM8250) 42 02 0b DSI panel");
MODULE_LICENSE("GPL");
