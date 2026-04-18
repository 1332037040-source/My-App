#include "WAVReader.h"
#include <iostream>
#include <cstring>
#include <sstream>

namespace WAVReader {

    DVector read_wav_ultimate(const std::string& wav_path, uint32_t& fs) {
        DVector audio_data;
        fs = 0;

        std::ifstream file(wav_path, std::ios::binary);
        if (!file) {
            std::cerr << "[Error] 无法打开文件: " << wav_path << std::endl;
            return audio_data;
        }

        char head[12];
        file.read(head, 12);
        if (file.gcount() != 12 || std::memcmp(head, "RIFF", 4) != 0 || std::memcmp(head + 8, "WAVE", 4) != 0) {
            std::cerr << "[Error] 非标准WAV文件: " << wav_path << std::endl;
            return audio_data;
        }

        WavFormat fmt;
        bool ok_fmt = false, ok_data = false;

        while (file.good()) {
            char id[4] = { 0 };
            uint32_t sz = 0;

            file.read(id, 4);
            file.read(reinterpret_cast<char*>(&sz), 4);
            if (!file) break;

            if (std::memcmp(id, "fmt ", 4) == 0) {
                file.read(reinterpret_cast<char*>(&fmt.audio_format), 2);
                file.read(reinterpret_cast<char*>(&fmt.num_channels), 2);
                file.read(reinterpret_cast<char*>(&fmt.sample_rate), 4);
                file.seekg(6, std::ios::cur); // byteRate + blockAlign
                file.read(reinterpret_cast<char*>(&fmt.bits_per_sample), 2);

                if (sz > 16) file.seekg(static_cast<std::streamoff>(sz - 16), std::ios::cur);
                ok_fmt = true;
            }
            else if (std::memcmp(id, "data", 4) == 0) {
                fmt.data_size = sz;
                fmt.data_pos = file.tellg();
                ok_data = true;
                break;
            }
            else {
                file.seekg(static_cast<std::streamoff>(sz), std::ios::cur);
            }
        }

        if (!ok_fmt || !ok_data) {
            std::cerr << "[Error] WAV格式解析失败: " << wav_path << std::endl;
            return audio_data;
        }

        fs = fmt.sample_rate;
        const size_t bps = fmt.bits_per_sample / 8;
        if (bps == 0 || fmt.num_channels == 0) {
            std::cerr << "[Error] WAV参数非法: bits/channels" << std::endl;
            return audio_data;
        }

        const size_t total_samples = fmt.data_size / (bps * fmt.num_channels);
        file.seekg(fmt.data_pos);

        std::vector<char> buf(fmt.data_size);
        file.read(buf.data(), static_cast<std::streamsize>(fmt.data_size));
        if (!file) {
            std::cerr << "[Error] WAV数据读取失败: " << wav_path << std::endl;
            return DVector{};
        }

        audio_data.resize(total_samples);
        size_t idx = 0;

        for (size_t i = 0; i + bps * fmt.num_channels <= fmt.data_size && idx < total_samples; i += bps * fmt.num_channels) {
            if (fmt.bits_per_sample == 16) {
                int16_t v = *reinterpret_cast<int16_t*>(buf.data() + i);
                audio_data[idx++] = static_cast<double>(v) / 32768.0;
            }
            else if (fmt.bits_per_sample == 8) {
                uint8_t v = static_cast<uint8_t>(buf[i]);
                audio_data[idx++] = (static_cast<double>(v) - 128.0) / 128.0;
            }
            else if (fmt.bits_per_sample == 32 && fmt.audio_format == 3) { // float PCM
                float v = *reinterpret_cast<float*>(buf.data() + i);
                audio_data[idx++] = static_cast<double>(v);
            }
        }

        return audio_data;
    }

    std::vector<WavData> read_multiple_wavs(const std::vector<std::string>& wav_paths) {
        std::vector<WavData> results;
        results.reserve(wav_paths.size());

        for (const auto& path : wav_paths) {
            WavData wd;
            wd.filePath = path;
            wd.data = read_wav_ultimate(path, wd.fs);
            wd.isSuccess = !wd.data.empty();
            results.push_back(std::move(wd));
        }

        return results;
    }

} // namespace WAVReader
