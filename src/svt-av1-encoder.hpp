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

#pragma once

#include <obs-module.h>
#include <util/platform.h>

#include <EbSvtAv1.h>
#include <EbSvtAv1Enc.h>

struct svt_av1_encoder {
	/* OBS Variables */
	obs_encoder_t *obs_encoder = nullptr;

	/* SVT-AV1 Variables */
	EbComponentType *svt_encoder = nullptr;
	EbSvtAv1EncConfiguration svt_config = {0};
	EbBufferHeaderType *buffer = nullptr;

	/* More Variables */
	video_format format = {};
	size_t plane_count = 0;
	uint8_t* i420_frame[3] = {nullptr};
	os_performance_token_t *perf_token = nullptr;
};

void* svt_av1_encoder_create(obs_data_t *settings, obs_encoder_t *encoder);
void svt_av1_encoder_destroy(void *data);
bool svt_av1_encoder_encode(void *data, encoder_frame *frame, encoder_packet *packet,
		    bool *received_packet);
