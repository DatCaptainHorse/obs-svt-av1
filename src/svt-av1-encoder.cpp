#include <plugin-support.h>
#include "svt-av1-encoder.hpp"
#include <vector>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <new>

// Helper to calculate buffer size for YUV420
static size_t get_yuv420_buffer_size(int width, int height, int bit_depth) {
    size_t size = width * height; // Y
    size += (width / 2) * (height / 2) * 2; // U + V
    if (bit_depth > 8) size *= 2; // 16-bit elements
    return size;
}

void SvtAv1Encoder::svt_log_callback(void* context, SvtAv1LogLevel level, const char* tag, const char* fmt, va_list args) {
    UNUSED_PARAMETER(context);
    UNUSED_PARAMETER(tag);
    // Map SVT log levels to OBS
    int obs_level = LOG_DEBUG;
    switch (level) {
        case SVT_AV1_LOG_FATAL: obs_level = LOG_ERROR; break;
        case SVT_AV1_LOG_ERROR: obs_level = LOG_ERROR; break;
        case SVT_AV1_LOG_WARN:  obs_level = LOG_WARNING; break;
        case SVT_AV1_LOG_INFO:  obs_level = LOG_INFO; break;
        case SVT_AV1_LOG_DEBUG: obs_level = LOG_DEBUG; break;
        default: break;
    }

    // We can't easily use obs_log with va_list safely across all platforms/versions here without a wrapper,
    // but typically we can format into a buffer.
    char buffer[4096];
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    obs_log(obs_level, "[SVT-AV1] %s", buffer);
}

SvtAv1Encoder::SvtAv1Encoder(obs_data_t* settings, obs_encoder_t* encoder)
    : obs_encoder_(encoder) {

    // Set log callback globally (SVT API is global for this)
    // Note: This might conflict if multiple instances set different contexts,
    // but the callback here is static and context-free mainly.
    svt_av1_set_log_callback(svt_log_callback, nullptr);

    // Initialize config
    svt_config_.enc_mode = 8; // Default

    if (init_svt()) {
       update_settings(settings);

       // Apply settings
       if (svt_av1_enc_set_parameter(svt_handle_, &svt_config_) != EB_ErrorNone) {
           obs_log(LOG_ERROR, "Failed to set SVT-AV1 parameters");
       }

       // Init encoder
       if (svt_av1_enc_init(svt_handle_) != EB_ErrorNone) {
           obs_log(LOG_ERROR, "Failed to initialize SVT-AV1 encoder");
       }

       // Create input buffer
       input_buffer_header_ = (EbBufferHeaderType*)bzalloc(sizeof(EbBufferHeaderType));
       input_buffer_header_->size = sizeof(EbBufferHeaderType);

       // Allocate the wrapper struct for planar pointers
       input_buffer_data_.resize(sizeof(EbSvtIOFormat));
       input_buffer_header_->p_buffer = input_buffer_data_.data();

       // Internal planar buffer allocation will happen on first frame or if size known
       video_t *video = obs_encoder_video(encoder);
       const struct video_output_info *voi = video_output_get_info(video);

       width_ = voi->width;
       height_ = voi->height;
       format_ = voi->format;

       // Check for 10-bit override
       bool force_10bit = obs_data_get_bool(settings, "enc_10bit");
       bit_depth_ = (format_ == VIDEO_FORMAT_P010 || format_ == VIDEO_FORMAT_I010 || force_10bit) ? 10 : 8;

       // If we are doing conversion, allocate buffer
       if (format_ == VIDEO_FORMAT_NV12 || format_ == VIDEO_FORMAT_P010) {
           size_t buf_size = get_yuv420_buffer_size(width_, height_, bit_depth_);
           planar_buffer_.resize(buf_size);
       }

       perf_token_ = os_request_high_performance("svt-av1 encoding");
    }
}

SvtAv1Encoder::~SvtAv1Encoder() {
    if (svt_handle_) {
        svt_av1_enc_deinit(svt_handle_);
        svt_av1_enc_deinit_handle(svt_handle_);
    }
    if (input_buffer_header_) {
        bfree(input_buffer_header_);
    }
    if (perf_token_) {
        os_end_high_performance(perf_token_);
    }
}

bool SvtAv1Encoder::init_svt() {
    return svt_av1_enc_init_handle(&svt_handle_, &svt_config_) == EB_ErrorNone;
}

void SvtAv1Encoder::update_settings(obs_data_t* settings) {
    video_t *video = obs_encoder_video(obs_encoder_);
    const struct video_output_info *voi = video_output_get_info(video);

    // Basic Video Info
    svt_config_.source_width = voi->width;
    svt_config_.source_height = voi->height;
    svt_config_.frame_rate_numerator = voi->fps_num;
    svt_config_.frame_rate_denominator = voi->fps_den;
    svt_config_.encoder_bit_depth = bit_depth_;
    svt_config_.encoder_color_format = EB_YUV420; // We convert everything to 420

    // Preset
    svt_config_.enc_mode = (int8_t)obs_data_get_int(settings, "enc_preset");

    // Rate Control
    const char* rc = obs_data_get_string(settings, "rc_mode");
    if (strcmp(rc, "CQP") == 0) {
        svt_config_.rate_control_mode = SVT_AV1_RC_MODE_CQP_OR_CRF;
        // For CQP in SVT, we often set Min/Max QP to same or use qp
        uint32_t qp = (uint32_t)obs_data_get_int(settings, "enc_qp");
        svt_config_.qp = qp;
        // Disable adaptive quantization for pure CQP if desired, or let SVT handle it
        // svt_config_.enable_adaptive_quantization = 0;
    } else if (strcmp(rc, "CRF") == 0) {
        svt_config_.rate_control_mode = SVT_AV1_RC_MODE_CQP_OR_CRF;
        uint32_t crf = (uint32_t)obs_data_get_int(settings, "enc_crf");
        // SVT maps QP field to CRF when in this mode if AQ is set correctly?
        // Actually SVT v3 has a specific CRF logic usually via qp field or separate.
        // Looking at Parameters.md: "CRF ... setting this value is similar to --rc 0 --aq-mode 2 --qp x"
        svt_config_.qp = crf;
        svt_config_.max_bit_rate = (uint32_t)(obs_data_get_int(settings, "enc_max_bitrate") * 1000);
    } else if (strcmp(rc, "VBR") == 0) {
        svt_config_.rate_control_mode = SVT_AV1_RC_MODE_VBR;
        svt_config_.target_bit_rate = (uint32_t)(obs_data_get_int(settings, "enc_bitrate") * 1000);
    } else if (strcmp(rc, "CBR") == 0) {
        svt_config_.rate_control_mode = SVT_AV1_RC_MODE_CBR;
        svt_config_.target_bit_rate = (uint32_t)(obs_data_get_int(settings, "enc_bitrate") * 1000);
    }

    // GOP
    int keyint = (int)obs_data_get_int(settings, "enc_keyint");
    svt_config_.intra_period_length = (keyint == -1) ? -2 : keyint; // -2 is auto in SVT

    // Profile/Tier
    svt_config_.profile = (EbAv1SeqProfile)obs_data_get_int(settings, "enc_profile");
    svt_config_.tier = (uint32_t)obs_data_get_int(settings, "enc_tier");

    // Lookahead
    svt_config_.look_ahead_distance = (uint32_t)obs_data_get_int(settings, "enc_lookahead");

    // SCD
    svt_config_.scene_change_detection = (uint32_t)obs_data_get_int(settings, "enc_scd");

    // Tune
    svt_config_.tune = (uint8_t)obs_data_get_int(settings, "enc_tune");

    // Threads
    svt_config_.level_of_parallelism = (uint32_t)obs_data_get_int(settings, "enc_threads");

    // Tiles
    svt_config_.tile_columns = (int32_t)obs_data_get_int(settings, "tile_cols");
    svt_config_.tile_rows = (int32_t)obs_data_get_int(settings, "tile_rows");

    // Film Grain
    svt_config_.film_grain_denoise_strength = (uint32_t)obs_data_get_int(settings, "film_grain");
    if (svt_config_.film_grain_denoise_strength > 0) {
        svt_config_.film_grain_denoise_apply = 1;
    }

    // Color / HDR
    int primaries = (int)obs_data_get_int(settings, "color_primaries");
    int transfer = (int)obs_data_get_int(settings, "color_trc");
    int matrix = (int)obs_data_get_int(settings, "color_matrix");
    int range = (int)obs_data_get_int(settings, "color_range");

    if (primaries != 2) svt_config_.color_primaries = (EbColorPrimaries)primaries;
    if (transfer != 2) svt_config_.transfer_characteristics = (EbTransferCharacteristics)transfer;
    if (matrix != 2) svt_config_.matrix_coefficients = (EbMatrixCoefficients)matrix;
    svt_config_.color_range = (EbColorRange)range;

    // 10-bit forcing validation
    if (bit_depth_ == 10) {
        svt_config_.encoder_bit_depth = 10;
    }
}

// ... helper conversion functions implemented previously ...

void SvtAv1Encoder::convert_nv12_to_i420(const uint8_t* luma, int luma_stride,
                                        const uint8_t* chroma, int chroma_stride,
                                        uint8_t* y, uint8_t* u, uint8_t* v,
                                        int width, int height) {
    // Copy Y plane
    for (int r = 0; r < height; ++r) {
        memcpy(y + r * width, luma + r * luma_stride, width);
    }

    // De-interleave UV plane
    int uv_width = width / 2;
    int uv_height = height / 2;

    for (int r = 0; r < uv_height; ++r) {
        const uint8_t* src_row = chroma + r * chroma_stride;
        uint8_t* u_row = u + r * uv_width;
        uint8_t* v_row = v + r * uv_width;

        for (int c = 0; c < uv_width; ++c) {
            u_row[c] = src_row[2 * c];
            v_row[c] = src_row[2 * c + 1];
        }
    }
}

void SvtAv1Encoder::convert_p010_to_i010(const uint8_t* luma, int luma_stride,
                                        const uint8_t* chroma, int chroma_stride,
                                        uint16_t* y, uint16_t* u, uint16_t* v,
                                        int width, int height) {
    // Y Plane
    for (int r = 0; r < height; ++r) {
        const uint16_t* src_row = reinterpret_cast<const uint16_t*>(luma + r * luma_stride);
        uint16_t* dst_row = y + r * width;
        for (int c = 0; c < width; ++c) {
            dst_row[c] = src_row[c] >> 6;
        }
    }

    // UV Plane
    int uv_width = width / 2;
    int uv_height = height / 2;

    for (int r = 0; r < uv_height; ++r) {
        const uint16_t* src_row = reinterpret_cast<const uint16_t*>(chroma + r * chroma_stride);
        uint16_t* u_row = u + r * uv_width;
        uint16_t* v_row = v + r * uv_width;

        for (int c = 0; c < uv_width; ++c) {
            u_row[c] = src_row[2 * c] >> 6;
            v_row[c] = src_row[2 * c + 1] >> 6;
        }
    }
}

void SvtAv1Encoder::convert_nv12_to_i010(const uint8_t* luma, int luma_stride,
                                        const uint8_t* chroma, int chroma_stride,
                                        uint16_t* y, uint16_t* u, uint16_t* v,
                                        int width, int height) {
    // Y Plane (8-bit -> 10-bit)
    for (int r = 0; r < height; ++r) {
        const uint8_t* src_row = luma + r * luma_stride;
        uint16_t* dst_row = y + r * width;
        for (int c = 0; c < width; ++c) {
            // Shift 8 bits to 10 bits (<< 2)
            dst_row[c] = (uint16_t)src_row[c] << 2;
        }
    }

    // UV Plane (8-bit -> 10-bit)
    int uv_width = width / 2;
    int uv_height = height / 2;

    for (int r = 0; r < uv_height; ++r) {
        const uint8_t* src_row = chroma + r * chroma_stride;
        uint16_t* u_row = u + r * uv_width;
        uint16_t* v_row = v + r * uv_width;

        for (int c = 0; c < uv_width; ++c) {
            u_row[c] = (uint16_t)src_row[2 * c] << 2;
            v_row[c] = (uint16_t)src_row[2 * c + 1] << 2;
        }
    }
}

void SvtAv1Encoder::convert_i420_to_i010(const uint8_t* luma, int luma_stride,
                                        const uint8_t* cb, int cb_stride,
                                        const uint8_t* cr, int cr_stride,
                                        uint16_t* y, uint16_t* u, uint16_t* v,
                                        int width, int height) {
    // Y Plane
    for (int r = 0; r < height; ++r) {
        const uint8_t* src_row = luma + r * luma_stride;
        uint16_t* dst_row = y + r * width;
        for (int c = 0; c < width; ++c) {
            dst_row[c] = (uint16_t)src_row[c] << 2;
        }
    }

    int uv_width = width / 2;
    int uv_height = height / 2;

    // U Plane
    for (int r = 0; r < uv_height; ++r) {
        const uint8_t* src_row = cb + r * cb_stride;
        uint16_t* dst_row = u + r * uv_width;
        for (int c = 0; c < uv_width; ++c) {
            dst_row[c] = (uint16_t)src_row[c] << 2;
        }
    }

    // V Plane
    for (int r = 0; r < uv_height; ++r) {
        const uint8_t* src_row = cr + r * cr_stride;
        uint16_t* dst_row = v + r * uv_width;
        for (int c = 0; c < uv_width; ++c) {
            dst_row[c] = (uint16_t)src_row[c] << 2;
        }
    }
}

bool SvtAv1Encoder::convert_frame(encoder_frame* frame, EbSvtIOFormat* buffer) {
    if (format_ == VIDEO_FORMAT_NV12) {
        if (bit_depth_ == 10) {
            // Force 8-bit NV12 to 10-bit I010
            size_t y_size = width_ * height_;
            size_t uv_size = (width_ / 2) * (height_ / 2);

            uint16_t* y = reinterpret_cast<uint16_t*>(planar_buffer_.data());
            uint16_t* u = y + y_size;
            uint16_t* v = u + uv_size;

            convert_nv12_to_i010(frame->data[0], frame->linesize[0],
                                 frame->data[1], frame->linesize[1],
                                 y, u, v, width_, height_);

            buffer->luma = reinterpret_cast<uint8_t*>(y);
            buffer->cb = reinterpret_cast<uint8_t*>(u);
            buffer->cr = reinterpret_cast<uint8_t*>(v);
            buffer->y_stride = width_ * 2; // 16-bit
            buffer->cb_stride = (width_ / 2) * 2;
            buffer->cr_stride = (width_ / 2) * 2;
        } else {
            // Standard NV12 to I420
            size_t y_size = width_ * height_;
            size_t uv_size = (width_ / 2) * (height_ / 2);

            uint8_t* y = planar_buffer_.data();
            uint8_t* u = y + y_size;
            uint8_t* v = u + uv_size;

            convert_nv12_to_i420(frame->data[0], frame->linesize[0],
                                 frame->data[1], frame->linesize[1],
                                 y, u, v, width_, height_);

            buffer->luma = y;
            buffer->cb = u;
            buffer->cr = v;
            buffer->y_stride = width_;
            buffer->cb_stride = width_ / 2;
            buffer->cr_stride = width_ / 2;
        }
    } else if (format_ == VIDEO_FORMAT_P010) {
        size_t y_size = width_ * height_;
        size_t uv_size = (width_ / 2) * (height_ / 2);

        uint16_t* y = reinterpret_cast<uint16_t*>(planar_buffer_.data());
        uint16_t* u = y + y_size;
        uint16_t* v = u + uv_size;

        convert_p010_to_i010(frame->data[0], frame->linesize[0],
                             frame->data[1], frame->linesize[1],
                             y, u, v, width_, height_);

        buffer->luma = reinterpret_cast<uint8_t*>(y);
        buffer->cb = reinterpret_cast<uint8_t*>(u);
        buffer->cr = reinterpret_cast<uint8_t*>(v);
        buffer->y_stride = width_ * 2;
        buffer->cb_stride = (width_ / 2) * 2;
        buffer->cr_stride = (width_ / 2) * 2;

    } else if (format_ == VIDEO_FORMAT_I420) {
        if (bit_depth_ == 10) {
            // Force 8-bit I420 to 10-bit I010
            size_t y_size = width_ * height_;
            size_t uv_size = (width_ / 2) * (height_ / 2);

            uint16_t* y = reinterpret_cast<uint16_t*>(planar_buffer_.data());
            uint16_t* u = y + y_size;
            uint16_t* v = u + uv_size;

            convert_i420_to_i010(frame->data[0], frame->linesize[0],
                                 frame->data[1], frame->linesize[1],
                                 frame->data[2], frame->linesize[2],
                                 y, u, v, width_, height_);

            buffer->luma = reinterpret_cast<uint8_t*>(y);
            buffer->cb = reinterpret_cast<uint8_t*>(u);
            buffer->cr = reinterpret_cast<uint8_t*>(v);
            buffer->y_stride = width_ * 2;
            buffer->cb_stride = (width_ / 2) * 2;
            buffer->cr_stride = (width_ / 2) * 2;
        } else {
            // Pass-through
            buffer->luma = frame->data[0];
            buffer->cb = frame->data[1];
            buffer->cr = frame->data[2];
            buffer->y_stride = frame->linesize[0];
            buffer->cb_stride = frame->linesize[1];
            buffer->cr_stride = frame->linesize[2];
        }
    } else if (format_ == VIDEO_FORMAT_I010) {
        buffer->luma = frame->data[0];
        buffer->cb = frame->data[1];
        buffer->cr = frame->data[2];
        buffer->y_stride = frame->linesize[0];
        buffer->cb_stride = frame->linesize[1];
        buffer->cr_stride = frame->linesize[2];
    } else {
        return false;
    }
    return true;
}

bool SvtAv1Encoder::encode(encoder_frame* frame, encoder_packet* packet, bool* received_packet) {
    if (!packet || !received_packet) return false;

    // Reset packet received state
    *received_packet = false;

    // Send Frame
    EbBufferHeaderType* input_buffer = input_buffer_header_;
    input_buffer->n_filled_len = 0; // Default to 0 if no frame (flush)
    input_buffer->flags = 0;
    input_buffer->p_app_private = nullptr;
    input_buffer->pic_type = EB_AV1_INVALID_PICTURE;
    input_buffer->metadata = nullptr;

    if (frame) {
        EbSvtIOFormat* buffer_fmt = (EbSvtIOFormat*)input_buffer->p_buffer;
        buffer_fmt->color_fmt = EB_YUV420; // All our inputs are converted to/are 420

        if (!convert_frame(frame, buffer_fmt)) {
            obs_log(LOG_ERROR, "Unsupported video format conversion");
            return false;
        }

        // n_filled_len isn't strictly used for planar input in same way as packed,
        // but typically size of YUV planes. SVT ignores it for pointers but good to set.
        input_buffer->n_filled_len = (uint32_t)(width_ * height_ * (bit_depth_ == 10 ? 2 : 1) * 3 / 2);
        input_buffer->pts = frame->pts;
    } else {
        input_buffer->flags = EB_BUFFERFLAG_EOS;
    }

    EbErrorType res = svt_av1_enc_send_picture(svt_handle_, input_buffer);
    if (res != EB_ErrorNone) {
        obs_log(LOG_ERROR, "svt_av1_enc_send_picture failed: %d", res);
        return false;
    }

    // Get Packet
    EbBufferHeaderType* output_buffer = nullptr;
    bool done = (frame == nullptr);
    res = svt_av1_enc_get_packet(svt_handle_, &output_buffer, done);

    if (res == EB_ErrorNone && output_buffer) {
        if (output_buffer->flags & EB_BUFFERFLAG_EOS) {
             // End of stream
        } else {
            *received_packet = true;

            // Resize persistent buffer and copy data
            // OBS packet data pointer is valid until the next call to encode/destroy
            packet_data_.resize(output_buffer->n_filled_len);
            memcpy(packet_data_.data(), output_buffer->p_buffer, output_buffer->n_filled_len);

            packet->data = packet_data_.data();
            packet->size = output_buffer->n_filled_len;
            packet->type = OBS_ENCODER_VIDEO;
            packet->pts = output_buffer->pts;
            packet->dts = output_buffer->dts;
            packet->keyframe = (output_buffer->pic_type == EB_AV1_KEY_PICTURE || output_buffer->pic_type == EB_AV1_FW_KEY_PICTURE);

            svt_av1_enc_release_out_buffer(&output_buffer);
        }
    } else if (res != EB_NoErrorEmptyQueue) {
         // Log error if not just empty queue
         // obs_log(LOG_WARNING, "svt_av1_enc_get_packet returned %d", res);
    }

    return true;
}

// Static Wrappers
void* SvtAv1Encoder::create(obs_data_t* settings, obs_encoder_t* encoder) {
    try {
        auto* enc = new SvtAv1Encoder(settings, encoder);
        if (!enc->is_valid()) {
            delete enc;
            return nullptr;
        }
        return enc;
    } catch (...) {
        return nullptr;
    }
}

void SvtAv1Encoder::destroy(void* data) {
    delete static_cast<SvtAv1Encoder*>(data);
}

bool SvtAv1Encoder::encode_wrapper(void* data, encoder_frame* frame, encoder_packet* packet, bool* received_packet) {
    return static_cast<SvtAv1Encoder*>(data)->encode(frame, packet, received_packet);
}
