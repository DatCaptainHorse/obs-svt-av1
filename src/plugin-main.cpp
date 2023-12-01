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

#include <svt-av1-encoder.hpp>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

static const char *svt_av1_encoder_getname(void *)
{
	return obs_module_text("SVT-AV1 (Direct)");
}

static obs_properties_t *svt_av1_get_properties(void *data)
{
	UNUSED_PARAMETER(data);
	obs_properties_t *props = obs_properties_create();

	// Presets (1-12)
	obs_property_t *prop = obs_properties_add_list(
		props, "enc_preset", obs_module_text("Encoder Preset"),
		OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(prop, obs_module_text("Preset 1"), 1);
	obs_property_list_add_int(prop, obs_module_text("Preset 2"), 2);
	obs_property_list_add_int(prop, obs_module_text("Preset 3"), 3);
	obs_property_list_add_int(prop, obs_module_text("Preset 4"), 4);
	obs_property_list_add_int(prop, obs_module_text("Preset 5"), 5);
	obs_property_list_add_int(prop, obs_module_text("Preset 6"), 6);
	obs_property_list_add_int(prop, obs_module_text("Preset 7"), 7);
	obs_property_list_add_int(prop, obs_module_text("Preset 8"), 8);
	obs_property_list_add_int(prop, obs_module_text("Preset 9"), 9);
	obs_property_list_add_int(prop, obs_module_text("Preset 10"), 10);
	obs_property_list_add_int(prop, obs_module_text("Preset 11"), 11);
	obs_property_list_add_int(prop, obs_module_text("Preset 12"), 12);

	// Bitrate
	obs_properties_add_int(props, "enc_bitrate",
			       obs_module_text("Bitrate (kbps)"), 1, 100000, 1);

	// Keyframe Interval (-2, -1, 1-a lot)
	obs_properties_add_int(props, "enc_keyint",
			       obs_module_text("Keyframe Interval"), -2, 1000, 1);

	// Scene Change Detection (0-1)
	prop = obs_properties_add_list(
		props, "enc_scd", obs_module_text("Scene Change Detection"),
		OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(prop, obs_module_text("Disabled"), 0);
	obs_property_list_add_int(prop, obs_module_text("Enabled"), 1);

	// Lookahead (-1, 0-120)
	obs_properties_add_int(props, "enc_lookahead",
			       obs_module_text("Lookahead"), -1, 120, 1);

	// Logical processors
	obs_properties_add_int(props, "enc_threads",
			       obs_module_text("Logical Processors"), 1, 64, 1);

	// Tune (0-1)
	prop = obs_properties_add_list(props, "enc_tune", obs_module_text("Tune"),
				       OBS_COMBO_TYPE_LIST,
				       OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(prop, obs_module_text("VQ"), 0);
	obs_property_list_add_int(prop, obs_module_text("PSNR"), 1);

	// Fast Decode (0-1)
	prop = obs_properties_add_list(
		props, "enc_fast_decode", obs_module_text("Fast Decode"),
		OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(prop, obs_module_text("Disabled"), 0);
	obs_property_list_add_int(prop, obs_module_text("Enabled"), 1);

	// Enable Overlays (0-1)
	prop = obs_properties_add_list(
		props, "enc_overlays", obs_module_text("Enable Overlays"),
		OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(prop, obs_module_text("Disabled"), 0);
	obs_property_list_add_int(prop, obs_module_text("Enabled"), 1);

	return props;
}

obs_encoder_info svt_av1_encoder_direct = {
	.id = "svt_av1_encoder_direct",
	.type = OBS_ENCODER_VIDEO,
	.codec = "av1",
	.get_name = svt_av1_encoder_getname,
	.create = svt_av1_encoder_create,
	.destroy = svt_av1_encoder_destroy,
	.encode = svt_av1_encoder_encode,
	.get_properties = svt_av1_get_properties,
	.caps = OBS_ENCODER_CAP_DYN_BITRATE,
};

bool obs_module_load()
{
	obs_register_encoder(&svt_av1_encoder_direct);

	obs_log(LOG_INFO, "plugin loaded successfully (version %s)",
		PLUGIN_VERSION);

	return true;
}

void obs_module_unload()
{
	obs_log(LOG_INFO, "plugin unloaded");
}
