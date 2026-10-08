#pragma once

#include <vorbis/vorbisenc.h>
#include <cmath>
#include <stdexcept>
#include <vector>
#include <cstdint>

// Generate real, freely redistributable audio, without game assets or ffmpeg.
inline std::vector<uint8_t> testVorbis(int channels = 1, int rate = 44100,
                                      int samples = 1257, int serial = 7) {
    vorbis_info info;
    vorbis_info_init(&info);
    if (vorbis_encode_init_vbr(&info, channels, rate, 0.3f)) {
        vorbis_info_clear(&info);
        throw std::runtime_error("Cannot initialize test Vorbis encoder");
    }
    vorbis_comment comment;
    vorbis_comment_init(&comment);
    vorbis_dsp_state dsp;
    vorbis_analysis_init(&dsp, &info);
    vorbis_block block;
    vorbis_block_init(&dsp, &block);
    ogg_stream_state stream;
    ogg_stream_init(&stream, serial);
    ogg_packet header, comments, codebooks;
    vorbis_analysis_headerout(&dsp, &comment, &header, &comments, &codebooks);
    for (auto* packet : {&header, &comments, &codebooks}) ogg_stream_packetin(&stream, packet);
    std::vector<uint8_t> output;
    ogg_page page;
    auto append = [&] {
        output.insert(output.end(), page.header, page.header + page.header_len);
        output.insert(output.end(), page.body, page.body + page.body_len);
    };
    while (ogg_stream_flush(&stream, &page)) append();
    float** pcm = vorbis_analysis_buffer(&dsp, samples);
    for (int c = 0; c < channels; ++c)
        for (int i = 0; i < samples; ++i)
            pcm[c][i] = 0.25f * std::sin((c + 1) * i * 0.071f);
    vorbis_analysis_wrote(&dsp, samples);
    vorbis_analysis_wrote(&dsp, 0);
    while (vorbis_analysis_blockout(&dsp, &block) == 1) {
        vorbis_analysis(&block, nullptr);
        vorbis_bitrate_addblock(&block);
        ogg_packet packet;
        while (vorbis_bitrate_flushpacket(&dsp, &packet)) {
            ogg_stream_packetin(&stream, &packet);
            while (ogg_stream_pageout(&stream, &page)) append();
        }
    }
    ogg_stream_clear(&stream);
    vorbis_block_clear(&block);
    vorbis_dsp_clear(&dsp);
    vorbis_comment_clear(&comment);
    vorbis_info_clear(&info);
    return output;
}
