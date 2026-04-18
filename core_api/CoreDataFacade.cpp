#include "core_api/CoreDataFacade.h"

#include <algorithm>
#include <cctype>
#include <exception>
#include <filesystem>
#include <string>
#include <vector>

#include "../io/ATFXReader.h"
#include "../io/HDFReader.h"
#include "../io/WAVReader.h"

namespace
{
    enum class FileKind
    {
        Unknown,
        Wav,
        Atfx,
        Hdf
    };

    std::string toLowerCopy(std::string s)
    {
        std::transform(s.begin(), s.end(), s.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    }

    FileKind detectFileKind(const std::string& filePath)
    {
        std::filesystem::path p(filePath);
        std::string ext = toLowerCopy(p.extension().string());

        if (ext == ".wav") return FileKind::Wav;
        if (ext == ".atfx") return FileKind::Atfx;
        if (ext == ".hdf" || ext == ".h5" || ext == ".hdf5") return FileKind::Hdf;
        return FileKind::Unknown;
    }

    std::string fileKindToString(FileKind kind)
    {
        switch (kind) {
        case FileKind::Wav:  return "wav";
        case FileKind::Atfx: return "atfx";
        case FileKind::Hdf:  return "hdf";
        default:             return "unknown";
        }
    }

    void fillSignalSlice(CoreSignalData& out,
        const std::vector<double>& allSamples,
        size_t startSample,
        size_t sampleCount,
        bool allowPartialRead)
    {
        out.totalSampleCount = allSamples.size();

        if (allSamples.empty()) {
            out.success = false;
            out.message = "no samples available";
            return;
        }

        if (startSample >= allSamples.size()) {
            out.success = false;
            out.message = "startSample out of range";
            return;
        }

        size_t remaining = allSamples.size() - startSample;
        size_t actualCount = sampleCount;

        if (sampleCount == 0) {
            actualCount = remaining;
        }
        else if (sampleCount > remaining) {
            if (!allowPartialRead) {
                out.success = false;
                out.message = "requested sample range exceeds available samples";
                return;
            }
            actualCount = remaining;
        }

        out.readStartIndex = startSample;
        out.readSampleCount = actualCount;
        out.samples.assign(allSamples.begin() + static_cast<std::ptrdiff_t>(startSample),
            allSamples.begin() + static_cast<std::ptrdiff_t>(startSample + actualCount));

        out.success = true;
        out.message = "ok";
    }

    CoreChannelInfo mapAtfxChannel(const ATFXChannelInfo& ch, double sampleRate, int index)
    {
        CoreChannelInfo out;
        out.id = index;
        out.name = ch.channelName;
        out.unit = ch.unit;
        out.sampleRate = sampleRate;
        out.sampleCount = ch.dataLength;
        out.durationSec = (sampleRate > 0.0)
            ? static_cast<double>(ch.dataLength) / sampleRate
            : 0.0;
        out.dof = ch.dof;
        return out;
    }

    CoreChannelInfo mapHdfChannel(const HDFChannelInfo& ch, double sampleRate, int index)
    {
        CoreChannelInfo out;
        out.id = index;
        out.name = ch.channelName;
        out.unit = ch.unit;
        out.sampleRate = sampleRate;
        out.sampleCount = ch.dataLength;
        out.durationSec = (sampleRate > 0.0)
            ? static_cast<double>(ch.dataLength) / sampleRate
            : 0.0;
        out.dof = ch.dof;
        return out;
    }

    std::vector<double> floatToDoubleVector(const std::vector<float>& input)
    {
        return std::vector<double>(input.begin(), input.end());
    }
}

CoreFileInfo CoreDataFacade::probeFile(const std::string& filePath) const
{
    CoreFileInfo info;
    info.filePath = filePath;

    try {
        if (filePath.empty()) {
            info.success = false;
            info.message = "filePath is empty";
            return info;
        }

        if (!std::filesystem::exists(filePath)) {
            info.success = false;
            info.message = "file does not exist";
            return info;
        }

        FileKind kind = detectFileKind(filePath);
        info.fileFormat = fileKindToString(kind);

        if (kind == FileKind::Unknown) {
            info.success = false;
            info.readable = false;
            info.message = "unsupported file type";
            return info;
        }

        auto listRes = listChannels(filePath);
        info.channelCount = listRes.channels.size();
        info.readable = listRes.success;
        info.success = listRes.success;
        info.message = listRes.message;

        return info;
    }
    catch (const std::exception& ex) {
        info.success = false;
        info.readable = false;
        info.message = ex.what();
        return info;
    }
    catch (...) {
        info.success = false;
        info.readable = false;
        info.message = "unknown exception in probeFile";
        return info;
    }
}

CoreChannelListResult CoreDataFacade::listChannels(const std::string& filePath) const
{
    CoreChannelListResult result;
    result.filePath = filePath;

    try {
        if (filePath.empty()) {
            result.success = false;
            result.message = "filePath is empty";
            return result;
        }

        if (!std::filesystem::exists(filePath)) {
            result.success = false;
            result.message = "file does not exist";
            return result;
        }

        FileKind kind = detectFileKind(filePath);
        result.fileFormat = fileKindToString(kind);

        switch (kind) {
        case FileKind::Atfx:
        {
            FFT11_ATFXReader reader;
            std::vector<ATFXChannelInfo> channels;
            double sampleRate = 0.0;

            if (!reader.GetAllChannels(filePath, channels, sampleRate)) {
                result.success = false;
                result.message = "failed to read ATFX channels";
                return result;
            }

            result.channels.reserve(channels.size());
            for (size_t i = 0; i < channels.size(); ++i) {
                result.channels.push_back(mapAtfxChannel(channels[i], sampleRate, static_cast<int>(i)));
            }

            result.success = true;
            result.message = "ok";
            return result;
        }

        case FileKind::Hdf:
        {
            FFT11_HDFReader reader;
            std::vector<HDFChannelInfo> channels;
            double sampleRate = 0.0;

            if (!reader.GetAllChannels(filePath, channels, sampleRate)) {
                result.success = false;
                result.message = "failed to read HDF channels";
                return result;
            }

            result.channels.reserve(channels.size());
            for (size_t i = 0; i < channels.size(); ++i) {
                result.channels.push_back(mapHdfChannel(channels[i], sampleRate, static_cast<int>(i)));
            }

            result.success = true;
            result.message = "ok";
            return result;
        }

        case FileKind::Wav:
        {
            uint32_t fs = 0;
            auto data = WAVReader::read_wav_ultimate(filePath, fs);

            // 这里先按 v1 单通道暴露
            CoreChannelInfo ch;
            ch.id = 0;
            ch.name = "Channel 1";
            ch.unit = "";
            ch.sampleRate = static_cast<double>(fs);
            ch.sampleCount = data.size();
            ch.durationSec = (fs > 0)
                ? static_cast<double>(data.size()) / static_cast<double>(fs)
                : 0.0;
            ch.dof = "";

            result.channels.push_back(ch);
            result.success = true;
            result.message = "ok";
            return result;
        }

        default:
            result.success = false;
            result.message = "unsupported file type";
            return result;
        }
    }
    catch (const std::exception& ex) {
        result.success = false;
        result.message = ex.what();
        return result;
    }
    catch (...) {
        result.success = false;
        result.message = "unknown exception in listChannels";
        return result;
    }
}

CoreSignalData CoreDataFacade::readSignal(const CoreDataRequest& request) const
{
    CoreSignalData out;
    out.filePath = request.filePath;
    out.channelName = request.channelName;

    try {
        if (request.filePath.empty()) {
            out.success = false;
            out.message = "filePath is empty";
            return out;
        }

        if (!std::filesystem::exists(request.filePath)) {
            out.success = false;
            out.message = "file does not exist";
            return out;
        }

        FileKind kind = detectFileKind(request.filePath);

        switch (kind) {
        case FileKind::Atfx:
        {
            if (request.channelName.empty()) {
                out.success = false;
                out.message = "channelName is empty";
                return out;
            }

            FFT11_ATFXReader reader;
            std::vector<float> raw;
            double fs = 0.0;

            if (!reader.ReadChannelData(request.filePath, request.channelName, raw, fs)) {
                out.success = false;
                out.message = "failed to read ATFX channel data";
                return out;
            }

            // 为了补 unit/dof，再查一次通道列表
            std::vector<ATFXChannelInfo> channels;
            double listFs = 0.0;
            std::string unit;
            if (reader.GetAllChannels(request.filePath, channels, listFs)) {
                auto it = std::find_if(channels.begin(), channels.end(),
                    [&](const ATFXChannelInfo& ch) { return ch.channelName == request.channelName; });
                if (it != channels.end()) {
                    unit = it->unit;
                }
            }

            out.sampleRate = fs;
            out.unit = unit;

            auto samples = floatToDoubleVector(raw);
            fillSignalSlice(out, samples, request.startSample, request.sampleCount, request.allowPartialRead);
            return out;
        }

        case FileKind::Hdf:
        {
            if (request.channelName.empty()) {
                out.success = false;
                out.message = "channelName is empty";
                return out;
            }

            FFT11_HDFReader reader;
            std::vector<float> raw;
            double fs = 0.0;

            if (!reader.ReadChannelData(request.filePath, request.channelName, raw, fs)) {
                out.success = false;
                out.message = "failed to read HDF channel data";
                return out;
            }

            std::vector<HDFChannelInfo> channels;
            double listFs = 0.0;
            std::string unit;
            if (reader.GetAllChannels(request.filePath, channels, listFs)) {
                auto it = std::find_if(channels.begin(), channels.end(),
                    [&](const HDFChannelInfo& ch) { return ch.channelName == request.channelName; });
                if (it != channels.end()) {
                    unit = it->unit;
                }
            }

            out.sampleRate = fs;
            out.unit = unit;

            auto samples = floatToDoubleVector(raw);
            fillSignalSlice(out, samples, request.startSample, request.sampleCount, request.allowPartialRead);
            return out;
        }

        case FileKind::Wav:
        {
            // v1：WAV 仅公开单通道 "Channel 1"
            if (!request.channelName.empty() && request.channelName != "Channel 1") {
                out.success = false;
                out.message = "wav currently supports only 'Channel 1' in CoreDataFacade v1";
                return out;
            }

            uint32_t fs = 0;
            auto raw = WAVReader::read_wav_ultimate(request.filePath, fs);

            out.sampleRate = static_cast<double>(fs);
            out.unit = "";

            // 假设 DVector 可直接构造为 std::vector<double> 风格
            std::vector<double> samples(raw.begin(), raw.end());
            fillSignalSlice(out, samples, request.startSample, request.sampleCount, request.allowPartialRead);
            return out;
        }

        default:
            out.success = false;
            out.message = "unsupported file type";
            return out;
        }
    }
    catch (const std::exception& ex) {
        out.success = false;
        out.message = ex.what();
        return out;
    }
    catch (...) {
        out.success = false;
        out.message = "unknown exception in readSignal";
        return out;
    }
}