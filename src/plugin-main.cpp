/*
OBS SVT-AV1 Encoder Plugin
Copyright (C) 2023 Kristian Ollikainen <Email Address>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#include <obs-module.h>
#include <plugin-support.h>

#include "svt-av1-encoder.hpp"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

static const char *svt_av1_encoder_getname(void *)
{
	return obs_module_text("SVT-AV1 (Direct)");
}

static void svt_av1_encoder_get_video_info(void *, struct video_scale_info *info)
{
	info->format = VIDEO_FORMAT_NV12; // Preferred
}

static void svt_av1_encoder_get_defaults(obs_data_t *settings)
{
	obs_data_set_default_int(settings, "enc_preset", 8);
	obs_data_set_default_string(settings, "rc_mode", "CQP");
	obs_data_set_default_int(settings, "enc_bitrate", 2000);
	obs_data_set_default_int(settings, "enc_max_bitrate", 0);
	obs_data_set_default_int(settings, "enc_qp", 35);
	obs_data_set_default_int(settings, "enc_crf", 35);
	obs_data_set_default_int(settings, "enc_keyint", -1);
	obs_data_set_default_int(settings, "enc_profile", 0); // Main
	obs_data_set_default_int(settings, "enc_tier", 0); // Main
	obs_data_set_default_int(settings, "enc_lookahead", -1);
	obs_data_set_default_int(settings, "enc_scd", 1);
	obs_data_set_default_int(settings, "enc_tune", 1); // PSNR
	obs_data_set_default_int(settings, "enc_threads", 0);
	obs_data_set_default_int(settings, "tile_rows", 0);
	obs_data_set_default_int(settings, "tile_cols", 0);
	obs_data_set_default_int(settings, "film_grain", 0);
	obs_data_set_default_bool(settings, "enc_10bit", false);

	// Color defaults
	obs_data_set_default_int(settings, "color_primaries", 2);
	obs_data_set_default_int(settings, "color_trc", 2);
	obs_data_set_default_int(settings, "color_matrix", 2);
	obs_data_set_default_int(settings, "color_range", 0);
}

static bool rc_modified(obs_properties_t *props, obs_property_t *p,
			obs_data_t *settings)
{
	const char *rc = obs_data_get_string(settings, "rc_mode");
	bool is_cbr_vbr = (strcmp(rc, "CBR") == 0 || strcmp(rc, "VBR") == 0);
	bool is_crf_cqp = (strcmp(rc, "CQP") == 0 || strcmp(rc, "CRF") == 0);

	obs_property_set_visible(obs_properties_get(props, "enc_bitrate"), is_cbr_vbr);
	obs_property_set_visible(obs_properties_get(props, "enc_max_bitrate"), is_crf_cqp); // Capped CRF
	obs_property_set_visible(obs_properties_get(props, "enc_qp"), strcmp(rc, "CQP") == 0);
	obs_property_set_visible(obs_properties_get(props, "enc_crf"), strcmp(rc, "CRF") == 0);
	obs_property_set_visible(obs_properties_get(props, "enc_lookahead"), is_cbr_vbr || is_crf_cqp); // Lookahead works for all? Usually VBR/CBR.

	return true;
}

static obs_properties_t *svt_av1_get_properties(void *data)
{
	UNUSED_PARAMETER(data);
	obs_properties_t *props = obs_properties_create();

	// Presets (0-13)
	obs_property_t *p = obs_properties_add_list(
		props, "enc_preset", obs_module_text("Encoder Preset"),
		OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);

	// Add presets 0-13
	for (int i = 0; i <= 13; i++) {
		char name[32];
		snprintf(name, sizeof(name), "Preset %d", i);
		obs_property_list_add_int(p, name, i);
	}

	// Rate Control
	p = obs_properties_add_list(props, "rc_mode", obs_module_text("Rate Control"),
				    OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(p, "CQP", "CQP");
	obs_property_list_add_string(p, "CRF", "CRF");
	obs_property_list_add_string(p, "VBR", "VBR");
	obs_property_list_add_string(p, "CBR", "CBR");
	obs_property_set_modified_callback(p, rc_modified);

	// Bitrate (VBR/CBR)
	obs_properties_add_int(props, "enc_bitrate",
			       obs_module_text("Bitrate (kbps)"), 50, 100000, 50);

	// Max Bitrate (CRF Cap)
	obs_properties_add_int(props, "enc_max_bitrate",
			       obs_module_text("Max Bitrate (kbps) (0=Unlimited)"), 0, 100000, 50);

	// QP (CQP)
	obs_properties_add_int(props, "enc_qp", obs_module_text("QP"), 0, 63, 1);

	// CRF (CRF)
	obs_properties_add_int(props, "enc_crf", obs_module_text("CRF"), 0, 63, 1); // 0-70 actually supported in v3

	// Keyframe Interval
	obs_properties_add_int(props, "enc_keyint",
			       obs_module_text("Keyframe Interval (frames, -1=auto)"), -1, 10000, 1); // -2 is auto in SVT?

	// Profile
	p = obs_properties_add_list(props, "enc_profile", obs_module_text("Profile"),
				    OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(p, "Main", 0);
	obs_property_list_add_int(p, "High", 1);
	obs_property_list_add_int(p, "Professional", 2);

	// Tier
	p = obs_properties_add_list(props, "enc_tier", obs_module_text("Tier"),
				    OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(p, "Main", 0);
	obs_property_list_add_int(p, "High", 1);

	// Lookahead
	obs_properties_add_int(props, "enc_lookahead",
			       obs_module_text("Lookahead (frames)"), -1, 120, 1);

	// Scene Change Detection
	p = obs_properties_add_list(
		props, "enc_scd", obs_module_text("Scene Change Detection"),
		OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(p, obs_module_text("Disabled"), 0);
	obs_property_list_add_int(p, obs_module_text("Enabled"), 1);

	// 10-bit input handling (Explicit high-bitdepth override if user wants to force)
	p = obs_properties_add_bool(props, "enc_10bit", obs_module_text("High Dynamic Range (10-bit)"));
    obs_property_set_long_description(p, "Forces 10-bit encoding if input is 8-bit, or passes through 10-bit input.");

    // Tune
	p = obs_properties_add_list(props, "enc_tune", obs_module_text("Tune"),
				    OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(p, "VQ", 0);
	obs_property_list_add_int(p, "PSNR", 1);
	obs_property_list_add_int(p, "SSIM", 2);

    // Color Properties (Important for HDR)
    // Primaries
    p = obs_properties_add_list(props, "color_primaries", obs_module_text("Color Primaries"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
    obs_property_list_add_int(p, "Auto/Default", 2);
    obs_property_list_add_int(p, "BT.709", 1);
    obs_property_list_add_int(p, "BT.601", 6);
    obs_property_list_add_int(p, "BT.2020", 9);

    // Transfer
    p = obs_properties_add_list(props, "color_trc", obs_module_text("Transfer Characteristics"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
    obs_property_list_add_int(p, "Auto/Default", 2);
    obs_property_list_add_int(p, "BT.709", 1);
    obs_property_list_add_int(p, "SRGB", 13);
    obs_property_list_add_int(p, "PQ (SMPTE 2084)", 16);
    obs_property_list_add_int(p, "HLG (BT.2100)", 18);

    // Matrix
    p = obs_properties_add_list(props, "color_matrix", obs_module_text("Matrix Coefficients"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
    obs_property_list_add_int(p, "Auto/Default", 2);
    obs_property_list_add_int(p, "BT.709", 1);
    obs_property_list_add_int(p, "BT.601", 6);
    obs_property_list_add_int(p, "BT.2020 (NCL)", 9);

    // Range
    p = obs_properties_add_list(props, "color_range", obs_module_text("Color Range"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
    obs_property_list_add_int(p, "Limited (Studio)", 0);
    obs_property_list_add_int(p, "Full", 1);

    // Threads (Parallelism)
	obs_properties_add_int(props, "enc_threads",
			       obs_module_text("Threads (0=Auto)"), 0, 64, 1);

    // Tile Rows
    obs_properties_add_int(props, "tile_rows", obs_module_text("Tile Rows (log2)"), 0, 6, 1);
    // Tile Cols
    obs_properties_add_int(props, "tile_cols", obs_module_text("Tile Cols (log2)"), 0, 4, 1);

    // Film Grain
    obs_properties_add_int(props, "film_grain", obs_module_text("Film Grain Synthesis (0-50)"), 0, 50, 1);

	return props;
}

obs_encoder_info svt_av1_encoder_direct = {
	.id = "svt_av1_encoder_direct",
	.type = OBS_ENCODER_VIDEO,
	.codec = "av1",
	.get_name = svt_av1_encoder_getname,
	.create = SvtAv1Encoder::create,
	.destroy = SvtAv1Encoder::destroy,
	.encode = SvtAv1Encoder::encode_wrapper,
	.get_defaults = svt_av1_encoder_get_defaults,
	.get_properties = svt_av1_get_properties,
	.get_video_info = svt_av1_encoder_get_video_info,
	.caps = OBS_ENCODER_CAP_DYN_BITRATE,
};

bool obs_module_load()
{
	obs_register_encoder(&svt_av1_encoder_direct);
	return true;
}

void obs_module_unload()
{
}
