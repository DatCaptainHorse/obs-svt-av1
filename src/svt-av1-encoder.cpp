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

#include <plugin-support.h>
#include <svt-av1-encoder.hpp>

#include <initializer_list>

static void log_svt_av1_config(const EbSvtAv1EncConfiguration &config)
{
	obs_log(LOG_INFO, "SVT-AV1 Encoder Configuration:");
	obs_log(LOG_INFO, "  Encoder Mode: %d", config.enc_mode);
	obs_log(LOG_INFO, "  Source Width: %d", config.source_width);
	obs_log(LOG_INFO, "  Source Height: %d", config.source_height);
	obs_log(LOG_INFO, "  Frame Rate: %d/%d", config.frame_rate_numerator,
		config.frame_rate_denominator);
	obs_log(LOG_INFO, "  Rate Control Mode: %d", config.rate_control_mode);
	obs_log(LOG_INFO, "  Target Bit Rate: %d", config.target_bit_rate);
	obs_log(LOG_INFO, "  Keyframe Interval: %d", config.intra_period_length);
	obs_log(LOG_INFO, "  Scene Change Detection: %d",
		config.scene_change_detection);
	obs_log(LOG_INFO, "  Lookahead Distance: %d", config.look_ahead_distance);
	obs_log(LOG_INFO, "  Enable Overlays: %d", config.enable_overlays);
	obs_log(LOG_INFO, "  Tune: %d", config.tune);
	obs_log(LOG_INFO, "  Fast Decode: %d", config.fast_decode);
	obs_log(LOG_INFO, "  Logical Processors: %d", config.logical_processors);
	obs_log(LOG_INFO, "  Encoder Color Format: %d",
		config.encoder_color_format);
	obs_log(LOG_INFO, "  Encoder Bit Depth: %d", config.encoder_bit_depth);
}

void *svt_av1_encoder_create(obs_data_t *settings, obs_encoder_t *encoder)
{
	obs_log(LOG_INFO, "creating SVT-AV1 encoder");

	auto *enc = static_cast<svt_av1_encoder *>(
		bzalloc(sizeof(svt_av1_encoder)));
	if (!enc) {
		obs_log(LOG_ERROR, "failed to allocate SVT-AV1 encoder");
		return nullptr;
	}

	enc->obs_encoder = encoder;

	if (const auto res = svt_av1_enc_init_handle(&enc->svt_encoder, nullptr,
						     &enc->svt_config);
	    res != EB_ErrorNone) {
		obs_log(LOG_ERROR, "failed to create SVT-AV1 encoder: %s", res);
		bfree(enc);
		return nullptr;
	}

	const auto video = obs_encoder_video(enc->obs_encoder);
	const auto voi = video_output_get_info(video);

	enc->svt_config.enc_mode = static_cast<int8_t>(obs_data_get_int(settings, "enc_preset"));
	enc->svt_config.pred_structure = SVT_AV1_PRED_RANDOM_ACCESS;
	enc->svt_config.source_width = voi->width;
	enc->svt_config.source_height = voi->height;
	enc->svt_config.frame_rate_numerator = voi->fps_num;
	enc->svt_config.frame_rate_denominator = voi->fps_den;
	enc->svt_config.color_range = voi->format == VIDEO_FORMAT_I420
					      ? EB_CR_FULL_RANGE
					      : EB_CR_STUDIO_RANGE;
	enc->svt_config.rate_control_mode = SVT_AV1_RC_MODE_VBR;
	enc->svt_config.target_bit_rate = static_cast<uint32_t>(obs_data_get_int(settings, "enc_bitrate") * 1000);
	enc->svt_config.scene_change_detection = static_cast<uint32_t>(obs_data_get_int(settings, "enc_scd"));
	enc->svt_config.look_ahead_distance = static_cast<uint32_t>(obs_data_get_int(settings, "enc_lookahead"));
	enc->svt_config.enable_overlays = static_cast<unsigned char>(obs_data_get_int(settings, "enc_overlays"));
	enc->svt_config.tune = static_cast<uint8_t>(obs_data_get_int(settings, "enc_tune"));
	enc->svt_config.fast_decode = static_cast<unsigned char>(obs_data_get_int(settings, "enc_fast_decode"));
	enc->svt_config.logical_processors = static_cast<uint32_t>(obs_data_get_int(settings, "enc_threads"));
	enc->svt_config.intra_period_length = static_cast<int32_t>(obs_data_get_int(settings, "enc_keyint"));

	enc->format = voi->format;
	switch (enc->format) {
	case VIDEO_FORMAT_I010:
	case VIDEO_FORMAT_P010:
		enc->svt_config.encoder_color_format = EB_YUV420;
		enc->svt_config.encoder_bit_depth = 10;
		enc->plane_count = 2;
		break;
	case VIDEO_FORMAT_I420:
		enc->svt_config.encoder_color_format = EB_YUV420;
		enc->svt_config.encoder_bit_depth = 8;
		enc->plane_count = 3;
		break;
	case VIDEO_FORMAT_NV12:
		// Allocate enc->i420_frame (uint8_t*[3]) memory
		enc->i420_frame[0] = static_cast<uint8_t *>(
			bzalloc(voi->width * voi->height));
		enc->i420_frame[1] = static_cast<uint8_t *>(
			bzalloc(voi->width * voi->height / 4));
		enc->i420_frame[2] = static_cast<uint8_t *>(
			bzalloc(voi->width * voi->height / 4));
	default:
		enc->svt_config.encoder_color_format = EB_YUV420;
		enc->svt_config.encoder_bit_depth = 8;
		enc->plane_count = 2;
		break;
	}

	if (const auto res = svt_av1_enc_set_parameter(enc->svt_encoder,
						       &enc->svt_config);
	    res != EB_ErrorNone) {
		obs_log(LOG_ERROR,
			"failed to set SVT-AV1 encoder parameters: %s", res);
		svt_av1_enc_deinit_handle(enc->svt_encoder);
		bfree(enc);
		return nullptr;
	}

	log_svt_av1_config(enc->svt_config);

	if (const auto res = svt_av1_enc_init(enc->svt_encoder);
	    res != EB_ErrorNone) {
		obs_log(LOG_ERROR, "failed to initialize SVT-AV1 encoder: %s",
			res);
		svt_av1_enc_deinit_handle(enc->svt_encoder);
		bfree(enc);
		return nullptr;
	}

	enc->buffer = static_cast<EbBufferHeaderType *>(
		bzalloc(sizeof(EbBufferHeaderType)));
	enc->buffer->size = sizeof(EbBufferHeaderType);

	enc->buffer->p_buffer =
		static_cast<uint8_t *>(bzalloc(sizeof(EbSvtIOFormat)));

	enc->perf_token = os_request_high_performance("svt-av1 encoding");

	return enc;
}

void svt_av1_encoder_destroy(void *data)
{
	auto *enc = static_cast<svt_av1_encoder *>(data);
	if (!enc)
		return;

	EbErrorType res = EB_ErrorNone;

	// Send EOS frame
	EbBufferHeaderType eos_buf;
	eos_buf.n_alloc_len = 0;
	eos_buf.n_filled_len = 0;
	eos_buf.n_tick_count = 0;
	eos_buf.p_app_private = nullptr;
	eos_buf.flags = EB_BUFFERFLAG_EOS;
	eos_buf.p_buffer = nullptr;
	eos_buf.metadata = nullptr;

	res = svt_av1_enc_send_picture(enc->svt_encoder, &eos_buf);
	if (res != EB_ErrorNone) {
		obs_log(LOG_ERROR,
			"failed to send EOS frame to SVT-AV1 encoder: %s", res);
	}

	// Drain
	EbBufferHeaderType *output_buf = nullptr;
	res = svt_av1_enc_get_packet(enc->svt_encoder, &output_buf, true);
	if (res != EB_ErrorNone) {
		obs_log(LOG_ERROR, "failed to drain SVT-AV1 encoder: %s", res);
	}

	os_end_high_performance(enc->perf_token);
	svt_av1_enc_deinit(enc->svt_encoder);
	svt_av1_enc_deinit_handle(enc->svt_encoder);
	bfree(enc->buffer->p_buffer);
	bfree(enc->buffer);
	bfree(enc);
}

// Convert NV12 to I420 (i420_frame*[3] variable)
// returns true if conversion was successful
static void convert_nv12_to_i420(svt_av1_encoder *enc, encoder_frame *frame,
				 uint32_t frame_width, uint32_t frame_height)
{
	// Get size of Y plane
	const int y_size = frame_width * frame_height;

	// Copy the Y plane data from input frame to output frame
	memcpy(enc->i420_frame[0], frame->data[0], y_size);

	// Set pointer to start of input UV plane data
	uint8_t *uv_plane = frame->data[1];

	for (uint32_t h = 0; h < frame_height / 2; ++h) {
		for (uint32_t w = 0; w < frame_width; w += 2) {
			enc->i420_frame[1][w / 2 + h * (frame_width / 2)] =
				uv_plane[w]; // U plane
			enc->i420_frame[2][w / 2 + h * (frame_width / 2)] =
				uv_plane[w + 1]; // V plane
		}
		uv_plane += frame_width;
	}
}

static void handle_packet(svt_av1_encoder *enc, encoder_packet *packet,
			  bool done_sending_pics, bool *received_packet)
{
	EbBufferHeaderType *packet_buffer;
	if (const EbErrorType error = svt_av1_enc_get_packet(
		    enc->svt_encoder, &packet_buffer, done_sending_pics);
	    error != EB_ErrorNone) {
		if (error != EB_NoErrorEmptyQueue) {
			obs_log(LOG_ERROR, "Failed to get encoded packet: %d",
				error);
		}
		*received_packet = false;
		return;
	}

	// Allocate memory
	packet->data =
		static_cast<uint8_t *>(bzalloc(packet_buffer->n_filled_len));
	// Copy the encoded data
	memcpy(packet->data, packet_buffer->p_buffer,
	       packet_buffer->n_filled_len);

	packet->size = packet_buffer->n_filled_len;
	packet->type = OBS_ENCODER_VIDEO;
	packet->pts = packet_buffer->pts;
	packet->dts = packet_buffer->dts;
	packet->keyframe = packet_buffer->pic_type == EB_AV1_KEY_PICTURE;

	svt_av1_enc_release_out_buffer(&packet_buffer);

	*received_packet = true;
}

bool svt_av1_encoder_encode(void *data, encoder_frame *frame,
			    encoder_packet *packet, bool *received_packet)
{
	auto *enc = static_cast<svt_av1_encoder *>(data);

	if (!packet || !received_packet || !enc)
		return false;

	const auto video = obs_encoder_video(enc->obs_encoder);
	const auto voi = video_output_get_info(video);

	enc->buffer->flags = 0;
	enc->buffer->p_app_private = nullptr;
	enc->buffer->pic_type = EB_AV1_INVALID_PICTURE;
	enc->buffer->metadata = nullptr;

	if (frame) {
		auto *p_buffer = reinterpret_cast<EbSvtIOFormat *>(
			enc->buffer->p_buffer);

		// If NV12, convert to I420
		if (enc->format == VIDEO_FORMAT_NV12) {
			convert_nv12_to_i420(enc, frame, voi->width,
					     voi->height);
			// If conversion is successful, the Y, U, and V planes of the frame are updated in enc->i420_frame
			p_buffer->luma = enc->i420_frame[0]; // Y plane
			p_buffer->cb = enc->i420_frame[1];   // U plane
			p_buffer->cr = enc->i420_frame[2];   // V plane

			p_buffer->y_stride = voi->width;                                   // Y stride
			p_buffer->cb_stride = p_buffer->cr_stride = voi->width / 2;        // U, V strides

			p_buffer->color_fmt = EB_YUV420;            // Define color format as YUV420
		} else {
			// For other formats, use the original data (assuming it is already in I420 or related format)
			p_buffer->luma = frame->data[0]; // Y plane
			p_buffer->cb = frame->data[1];   // U plane
			p_buffer->cr = frame->data[2];   // V plane
		}

		// Frame size as n_filled_len
		enc->buffer->n_filled_len =
			voi->height * voi->width *
			static_cast<uint32_t>(enc->plane_count * 3 / 2);
		enc->buffer->pts = frame->pts;
	}

	if (const auto res =
		    svt_av1_enc_send_picture(enc->svt_encoder, enc->buffer);
	    res != EB_ErrorNone) {
		obs_log(LOG_ERROR,
			"failed to send picture to SVT-AV1 encoder: %s", res);
		return false;
	}

	handle_packet(enc, packet, frame == nullptr, received_packet);

	return true;
}
