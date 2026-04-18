#include "DataReaderService.h"
#include "../io/WAVReader.h"
#include "../domain/FileTypeUtils.h"

bool DataReaderService::ReadSignal(const Job& job,
    const FileItem& file,
    SignalData& out,
    std::string& err) const
{
    out = SignalData{};
    err.clear();

    // 与你现有 FFTExecutor 的判定逻辑保持一致：
    // 1) job.isATFX 优先
    // 2) file.ext == "hdf/h5/hdf5"
    // 3) 其他按 wav 处理
    if (job.isATFX) {
        return ReadATFX(job, file, out, err);
    }

    if (IsHdfExt(file.ext)) {
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

    std::vector<float> signal;
    double fs = 0.0;
    if (!atfx.ReadChannelData(file.path, ch.channelName, signal, fs)) {
        err = "读取ATFX通道失败";
        return false;
    }

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
    return true;
}

bool DataReaderService::ReadHDF(const Job& job,
    const FileItem& file,
    SignalData& out,
    std::string& err) const
{
    FFT11_HDFReader hdf;
    if (file.channels.empty()) {
        err = "HDF channel list is empty";
        return false;
    }

    size_t chIdx = job.channelIdx;
    if (chIdx >= file.channels.size()) chIdx = 0;
    const auto& ch = file.channels[chIdx];

    std::vector<float> signal;
    double fs = 0.0;
    if (!hdf.ReadChannelData(file.path, ch.channelName, signal, fs)) {
        err = "读取HDF通道失败";
        return false;
    }

    if (signal.empty()) {
        err = "HDF通道数据为空";
        return false;
    }
    if (fs <= 0.0) {
        err = "HDF非时域数据或采样率无效，当前流程仅支持时域FFT";
        return false;
    }

    out.samples.assign(signal.begin(), signal.end());
    out.fs = fs;
    out.channelName = ch.channelName;
    out.unit = ch.unit;
    out.sourcePath = file.path;
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

    out.samples = wav.data; // 你现有代码中 wav.data 已经是 vector<double>
    out.fs = static_cast<double>(wav.fs);
    out.channelName = "CH1";
    out.unit = "-";
    out.sourcePath = wav.filePath;
    return true;
}
