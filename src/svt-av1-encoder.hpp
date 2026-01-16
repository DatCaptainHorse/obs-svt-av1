#pragma once

#include <obs-module.h>
#include <util/platform.h>
#include <EbSvtAv1.h>
#include <EbSvtAv1Enc.h>

#include <memory>
#include <vector>
#include <span>
#include <mutex>
#include <optional>

class SvtAv1Encoder {
public:
    SvtAv1Encoder(obs_data_t* settings, obs_encoder_t* encoder);
    ~SvtAv1Encoder();

    bool encode(encoder_frame* frame, encoder_packet* packet, bool* received_packet);

    // Prevent copying
    SvtAv1Encoder(const SvtAv1Encoder&) = delete;
    SvtAv1Encoder& operator=(const SvtAv1Encoder&) = delete;

    static void* create(obs_data_t* settings, obs_encoder_t* encoder);
    static void destroy(void* data);
    static bool encode_wrapper(void* data, encoder_frame* frame, encoder_packet* packet, bool* received_packet);

private:
    bool init_svt();
    void update_settings(obs_data_t* settings);
    bool convert_frame(encoder_frame* frame, EbSvtIOFormat* buffer);

    // Optimized converters
    void convert_nv12_to_i420(const uint8_t* luma, int luma_stride,
                              const uint8_t* chroma, int chroma_stride,
                              uint8_t* y, uint8_t* u, uint8_t* v,
                              int width, int height);

    void convert_p010_to_i010(const uint8_t* luma, int luma_stride,
                              const uint8_t* chroma, int chroma_stride,
                              uint16_t* y, uint16_t* u, uint16_t* v,
                              int width, int height);

    void convert_nv12_to_i010(const uint8_t* luma, int luma_stride,
                              const uint8_t* chroma, int chroma_stride,
                              uint16_t* y, uint16_t* u, uint16_t* v,
                              int width, int height);

    void convert_i420_to_i010(const uint8_t* luma, int luma_stride,
                              const uint8_t* cb, int cb_stride,
                              const uint8_t* cr, int cr_stride,
                              uint16_t* y, uint16_t* u, uint16_t* v,
                              int width, int height);

    obs_encoder_t* obs_encoder_;
    EbComponentType* svt_handle_ = nullptr;
    EbSvtAv1EncConfiguration svt_config_ = {};

    // Buffer management
    EbBufferHeaderType* input_buffer_header_ = nullptr;
    std::vector<uint8_t> input_buffer_data_; // Holds EbSvtIOFormat struct

    // Format info
    video_format format_ = VIDEO_FORMAT_NONE;
    int width_ = 0;
    int height_ = 0;
    int bit_depth_ = 8;

    // Internal planar buffers for conversion
    std::vector<uint8_t> planar_buffer_;

    // Output packet data buffer (persistent to avoid leak)
    std::vector<uint8_t> packet_data_;

    os_performance_token_t* perf_token_ = nullptr;

    // Logging callback
    static void svt_log_callback(void* context, SvtAv1LogLevel level, const char* tag, const char* fmt, va_list args);

    bool is_valid() const { return svt_handle_ != nullptr; }
};
