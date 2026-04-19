#include "DataReaderService.h"
#include "../io/WAVReader.h"

#include <iostream>
#include <limits>
#include <algorithm>
#include <cctype>

namespace
{
    void PrintVectorPreview(const std::vector<double>& x, const std::string& name)
    {
        if (x.empty()) {
            std::cout << "[DEBUG] " << name << ": empty" << std::endl;
            return;
        }

        const size_t mid = x.size() / 2;
        double xmin = std::numeric_limits<double>::infinity();
        double xmax = -std::numeric_limits<double>::infinity();

        for (double v : x) {
            xmin = std::min(xmin, v);
            xmax = std::max(xmax, v);
        }

        std::cout << "[DEBUG] " << name
            << ": size=" << x.size()
            << ", first=" << x.front()
            << ", mid=" << x[mid]
            << ", last=" << x.back()
            << ", min=" << xmin
            << ", max=" << xmax
            << std::endl;
    }

    void PrintVectorPreviewF32(const std::vector<float>& x, const std::string& name)
    {
        if (x.empty()) {
            std::cout << "[DEBUG] " << name << ": empty" << std::endl;
            return;
        }

        const size_t mid = x.size() / 2;
        float xmin = std::numeric_limits<float>::infinity();
        float xmax = -std::numeric_limits<float>::infinity();

        for (float v : x) {
            xmin = std::min(xmin, v);
            xmax = std::max(xmax, v);
        }

        std::cout << "[DEBUG] " << name
            << ": size=" << x.size()
            << ", first=" << x.front()
            << ", mid=" << x[mid]
            << ", last=" << x.back()
            << ", min=" << xmin
            << ", max=" << xmax
            << std::endl;
    }

    static std::string ToLowerCopy(std::string s)
    {
        std::transform(s.begin(), s.end(), s.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    }

    // HDF里某些文件头给的是扫描时基(如289k)，而声学通道做FFT应使用有效采样率(如48k)。
    // 这里做一个保守对齐：仅对Pa单位/声学通道名且fs落在289k附近时，修正到48k。
    static double NormalizeHdfFsForAcousticChannel(
        const std::string& channelName,
        const std::string& unit,
        double fs)
    {
        const std::string n = ToLowerCopy(channelName);
        const std::string u = ToLowerCopy(unit);

        const bool isAcoustic =
            (n.find("mic") != std::string::npos) ||
            (n.find("hms") != std::string::npos) ||
            (u == "pa");

        // 仅在明显是扫描时基的范围内修正，避免误伤
        if (isAcoustic && fs > 200000.0 && fs < 400000.0) {
            std::cout << "[DEBUG] HDF fs override for acoustic channel: "
                << fs << " -> 48000" << std::endl;
            return 48000.0;
        }

        return fs;
    }
}

bool DataReaderService::ReadSignal(const Job& job,
    const FileItem& file,
    SignalData& out,
    std::string& err) const
{
    out = SignalData{};
    err.clear();

    std::cout << "[DEBUG] DataReaderService::ReadSignal"
        << " file=" << file.path
        << ", ext=" << file.ext
        << ", job.isATFX=" << job.isATFX
        << ", channelIdx=" << job.channelIdx
        << ", mode=" << static_cast<int>(job.mode)
        << std::endl;

    // 1) job.isATFX 优先
    // 2) file.ext == "hdf"
    // 3) 其他按 wav 处理
    if (job.isATFX) {
        return ReadATFX(job, file, out, err);
    }

    if (file.ext == "hdf") {
        return ReadHDF(job, file, out, err);
    }

    return ReadWAV(file, out, err);
}

bool DataReaderService::ReadATFX(const Job& job,
    const FileItem& file,
    SignalData& out,
    std::string& err) const
{
    FFT11_ATFXReader atfx;

    if (file.channels.empty()) {
        err = "ATFX通道列表为空";
        return false;
    }

    size_t chIdx = job.channelIdx;
    if (chIdx >= file.channels.size()) chIdx = 0;
    const auto& ch = file.channels[chIdx];

    std::cout << "[DEBUG] ReadATFX:"
        << " path=" << file.path
        << ", chIdx=" << chIdx
        << ", channelName=" << ch.channelName
        << std::endl;

    std::vector<float> signal;
    double fs = 0.0;
    if (!atfx.ReadChannelData(file.path, ch.channelName, signal, fs)) {
        err = "读取ATFX通道失败";
        return false;
    }

    PrintVectorPreviewF32(signal, "ATFX signal raw");

    if (signal.empty()) {
        err = "ATFX通道数据为空";
        return false;
    }
    if (fs <= 0.0) {
        err = "ATFX采样率无效";
        return false;
    }

    out.samples.assign(signal.begin(), signal.end());
    out.fs = fs;
    out.channelName = ch.channelName;
    out.unit = ch.unit;
    out.sourcePath = file.path;

    std::cout << "[DEBUG] ReadATFX done:"
        << " fs=" << out.fs
        << ", samples=" << out.samples.size()
        << ", channel=" << out.channelName
        << ", unit=" << out.unit
        << std::endl;

    PrintVectorPreview(out.samples, "ATFX signal out.samples");
    return true;
}

bool DataReaderService::ReadHDF(const Job& job,
    const FileItem& file,
    SignalData& out,
    std::string& err) const
{
    FFT11_HDFReader hdf;
    std::vector<HDFChannelInfo> hdfChannels;
    double fsMeta = 0.0;

    if (!hdf.GetAllChannels(file.path, hdfChannels, fsMeta) || hdfChannels.empty()) {
        err = "读取HDF通道列表失败";
        return false;
    }

    size_t chIdx = job.channelIdx;
    if (chIdx >= hdfChannels.size()) chIdx = 0;
    const auto& ch = hdfChannels[chIdx];

    std::cout << "[DEBUG] ReadHDF:"
        << " path=" << file.path
        << ", chIdx=" << chIdx
        << ", channelName=" << ch.channelName
        << ", metaFs=" << fsMeta
        << ", totalChannels=" << hdfChannels.size()
        << ", dataLength=" << ch.dataLength
        << ", dataOffset=" << ch.dataOffset
        << ", unit=" << ch.unit
        << ", dataType=" << ch.dataType
        << std::endl;

    std::vector<float> signal;
    double fs = 0.0;
    if (!hdf.ReadChannelData(file.path, ch.channelName, signal, fs)) {
        err = "读取HDF通道失败";
        return false;
    }

    PrintVectorPreviewF32(signal, "HDF signal raw");

    if (signal.empty()) {
        err = "HDF通道数据为空";
        return false;
    }
    if (fs <= 0.0) {
        err = "HDF非时域数据或采样率无效，当前流程仅支持时域FFT";
        return false;
    }

    // 关键：对声学通道做有效采样率归一（与Artemis对齐）
    fs = NormalizeHdfFsForAcousticChannel(ch.channelName, ch.unit, fs);

    out.samples.assign(signal.begin(), signal.end());
    out.fs = fs;
    out.channelName = ch.channelName;
    out.unit = ch.unit;
    out.sourcePath = file.path;

    std::cout << "[DEBUG] ReadHDF done:"
        << " fs=" << out.fs
        << ", samples=" << out.samples.size()
        << ", channel=" << out.channelName
        << ", unit=" << out.unit
        << std::endl;

    PrintVectorPreview(out.samples, "HDF signal out.samples");
    return true;
}

bool DataReaderService::ReadWAV(const FileItem& file,
    SignalData& out,
    std::string& err) const
{
    std::vector<std::string> one{ file.path };
    auto wavs = WAVReader::read_multiple_wavs(one);
    if (wavs.empty() || !wavs[0].isSuccess) {
        err = "WAV读取失败";
        return false;
    }

    auto wav = wavs[0];
    if (wav.data.empty()) {
        err = "WAV数据为空";
        return false;
    }
    if (wav.fs == 0) {
        err = "WAV采样率无效";
        return false;
    }

    out.samples = wav.data;
    out.fs = static_cast<double>(wav.fs);
    out.channelName = "CH1";
    out.unit = "-";
    out.sourcePath = wav.filePath;

    std::cout << "[DEBUG] ReadWAV done:"
        << " fs=" << out.fs
        << ", samples=" << out.samples.size()
        << ", path=" << out.sourcePath
        << std::endl;

    PrintVectorPreview(out.samples, "WAV signal out.samples");
    return true;
}