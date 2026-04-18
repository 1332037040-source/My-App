#pragma once
#include "core/common.h"
#include <fstream>
#include <vector>
#include <string>

namespace WAVReader {

    struct WavData {
        DVector data;
        uint32_t fs = 0;
        std::string filePath;
        bool isSuccess = false;
    };

    struct WavFormat {
        uint16_t audio_format = 0;
        uint16_t num_channels = 0;
        uint32_t sample_rate = 0;
        uint16_t bits_per_sample = 0;
        uint32_t data_size = 0;
        std::streampos data_pos = 0;
    };

    DVector read_wav_ultimate(const std::string& wav_path, uint32_t& fs);
    std::vector<WavData> read_multiple_wavs(const std::vector<std::string>& wav_paths);
}
