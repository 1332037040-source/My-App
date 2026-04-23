#include "FFTvsRpmFlow.h"

#include "../services/DataReaderService.h"
#include "../services/ExportService.h"
#include "../features/fft_vs_time/FFTvsTimeAnalyzer.h"
#include "../features/fft_vs_rpm/FFTvsRpmMapper.h"
#include "../features/order_tracking/AngleResampler.h"
#include "../io/ATFXReader.h"
#include "../io/HDFReader.h"
#include "../core/Utils.h"

#include <string>
#include <vector>
#include <cmath>
#include <limits>
#include <iostream>
#include <algorithm>
#include <iomanip>

namespace
{
    constexpr double kManualTimeOffsetSec = 0.55;
    constexpr bool kUseHalfWindowCenter = true;

    constexpr bool kUseOrderTracking = false;
    constexpr double kSamplesPerRev = 1024.0;

    bool ReadRpmChannel(
        const FileItem& file,
        const std::string& rpmChannelName,
        std::vector<double>& rpmSignal,
        double& fsRpm,
        std::string& err)
    {
        rpmSignal.clear();
        fsRpm = 0.0;
        err.clear();

        if (rpmChannelName.empty()) {
            err = "rpmChannelName 为空";
            return false;
        }

        std::vector<float> temp;

        if (file.ext == "atfx") {
            FFT11_ATFXReader reader;
            if (!reader.ReadChannelData(file.path, rpmChannelName, temp, fsRpm)) {
                err = "ATFX rpm通道读取失败: " + rpmChannelName;
                return false;
            }
        }
        else if (file.ext == "hdf") {
            FFT11_HDFReader reader;
            if (!reader.ReadChannelData(file.path, rpmChannelName, temp, fsRpm)) {
                err = "HDF rpm通道读取失败: " + rpmChannelName;
                return false;
            }
        }
        else {
            err = "FFT vs rpm 当前仅支持 ATFX/HDF";
            return false;
        }

        if (temp.empty()) {
            err = "rpm通道数据为空";
            return false;
        }
        if (!(fsRpm > 0.0)) {
            err = "rpm通道采样率无效";
            return false;
        }

        rpmSignal.resize(temp.size());
        for (size_t i = 0; i < temp.size(); ++i) {
            rpmSignal[i] = static_cast<double>(temp[i]);
        }

        return true;
    }

    std::vector<double> BuildFrameTimeSecFromSpectrogram(const Spectrogram& sp)
    {
        if (!sp.frameTimeSec.empty() && sp.frameTimeSec.size() == sp.timeBins) {
            return sp.frameTimeSec;
        }

        std::vector<double> frameTimeSec(sp.timeBins, 0.0);
        if (sp.timeBins == 0 || sp.fs <= 0.0) return frameTimeSec;

        const size_t hop = (sp.hopSize > 0 ? sp.hopSize : sp.blockSize);
        const double dt = 1.0 / sp.fs;

        for (size_t t = 0; t < sp.timeBins; ++t) {
            frameTimeSec[t] = static_cast<double>(t * hop) * dt;
        }

        return frameTimeSec;
    }

    void ApplyTimeOffset(std::vector<double>& frameTimeSec, double offsetSec)
    {
        if (frameTimeSec.empty()) return;
        if (std::abs(offsetSec) < 1e-18) return;

        for (double& t : frameTimeSec) {
            t += offsetSec;
            if (t < 0.0) t = 0.0;
        }
    }

    std::vector<double> ResampleRpmToMainAxis(
        const std::vector<double>& rpmSignal,
        size_t mainSamples)
    {
        std::vector<double> out(mainSamples, 0.0);
        if (rpmSignal.empty() || mainSamples == 0) return out;

        if (rpmSignal.size() == 1 || mainSamples == 1) {
            std::fill(out.begin(), out.end(), rpmSignal.front());
            return out;
        }

        const double scale =
            static_cast<double>(rpmSignal.size() - 1) /
            static_cast<double>(mainSamples - 1);

        for (size_t i = 0; i < mainSamples; ++i) {
            const double x = static_cast<double>(i) * scale;
            const size_t i0 = static_cast<size_t>(std::floor(x));
            const size_t i1 = (i0 + 1 < rpmSignal.size()) ? (i0 + 1) : i0;
            const double a = x - static_cast<double>(i0);
            out[i] = rpmSignal[i0] * (1.0 - a) + rpmSignal[i1] * a;
        }
        return out;
    }

    std::vector<std::vector<double>> BuildFreqFrames(const RpmSpectrogram& rpmSp)
    {
        std::vector<std::vector<double>> freqFrames;
        if (rpmSp.rpmBins == 0 || rpmSp.freqBins == 0 || rpmSp.fs <= 0.0 || rpmSp.blockSize == 0) {
            return freqFrames;
        }

        freqFrames.resize(rpmSp.rpmBins, std::vector<double>(rpmSp.freqBins, 0.0));
        const double df = rpmSp.fs / static_cast<double>(rpmSp.blockSize);
        for (size_t r = 0; r < rpmSp.rpmBins; ++r) {
            for (size_t f = 0; f < rpmSp.freqBins; ++f) {
                freqFrames[r][f] = static_cast<double>(f) * df;
            }
        }
        return freqFrames;
    }

    std::vector<std::vector<double>> BuildAmpFrames(const RpmSpectrogram& rpmSp)
    {
        std::vector<std::vector<double>> ampFrames;
        if (rpmSp.rpmBins == 0 || rpmSp.freqBins == 0 || rpmSp.dataDb.empty()) {
            return ampFrames;
        }

        ampFrames.resize(rpmSp.rpmBins, std::vector<double>(rpmSp.freqBins, 0.0));
        for (size_t r = 0; r < rpmSp.rpmBins; ++r) {
            for (size_t f = 0; f < rpmSp.freqBins; ++f) {
                ampFrames[r][f] = rpmSp.at(r, f);
            }
        }
        return ampFrames;
    }
}

JobResult FFTvsRpmFlow::Run(const Job& job, const FileItem& file)
{
    JobResult r;
    r.ok = false;
    r.message = "未开始";

    if (job.mode != AnalysisMode::FFT_VS_RPM) {
        r.message = "FFTvsRpmFlow仅支持AnalysisMode::FFT_VS_RPM";
        return r;
    }

    if (job.rpmChannelName.empty()) {
        r.message = "缺少 rpm 通道名";
        return r;
    }

    DataReaderService reader;
    ExportService exportSvc;

    SignalData sig;
    std::string err;
    if (!reader.ReadSignal(job, file, sig, err)) {
        r.message = err.empty() ? "读取主信号失败" : err;
        return r;
    }

    std::vector<double> rpmSignalRaw;
    double fsRpm = 0.0;
    if (!ReadRpmChannel(file, job.rpmChannelName, rpmSignalRaw, fsRpm, err)) {
        r.message = err.empty() ? "读取rpm信号失败" : err;
        return r;
    }

    std::vector<double> rpmOnMainAxis = ResampleRpmToMainAxis(rpmSignalRaw, sig.samples.size());

    std::vector<double> analysisSignal;
    double analysisFs = sig.fs;

    if (kUseOrderTracking) {
        std::vector<double> xTheta, tTheta;
        const double dTheta = 2.0 * M_PI / kSamplesPerRev;

        bool ok = OrderTracking::ResampleByAngle(
            sig.samples, rpmOnMainAxis, sig.fs, dTheta, xTheta, tTheta);

        if (!ok || xTheta.empty()) {
            r.message = "AngleResampler失败";
            return r;
        }

        double rpmMean = 0.0;
        for (double v : rpmOnMainAxis) rpmMean += v;
        rpmMean /= std::max<size_t>(size_t(1), rpmOnMainAxis.size());

        analysisFs = kSamplesPerRev * (std::max(1e-6, rpmMean) / 60.0);
        analysisSignal = std::move(xTheta);
    }
    else {
        analysisSignal = sig.samples;
        analysisFs = sig.fs;
    }

    Spectrogram sp = FFTvsTimeAnalyzer::Compute(analysisSignal, analysisFs, job.params);
    if (sp.timeBins == 0 || sp.freqBins == 0 || sp.dataLinear.empty()) {
        r.message = "FFT vs time 计算失败";
        return r;
    }

    std::vector<double> frameTime = BuildFrameTimeSecFromSpectrogram(sp);
    if (frameTime.size() != sp.timeBins) {
        r.message = "frameTimeSec 构建失败";
        return r;
    }

    const double halfWindowOffset =
        (kUseHalfWindowCenter && sp.fs > 0.0)
        ? (0.5 * static_cast<double>(sp.blockSize) / sp.fs)
        : 0.0;

    ApplyTimeOffset(frameTime, kManualTimeOffsetSec + halfWindowOffset);

    RpmSpectrogram rpmSp = FFTvsRpmMapper::MapTimeToRpm(
        sp, frameTime, rpmOnMainAxis, sig.fs,
        (job.rpmBinStep > 0.0 ? job.rpmBinStep : 50.0));

    if (rpmSp.rpmBins == 0 || rpmSp.freqBins == 0 || rpmSp.dataDb.empty()) {
        r.message = "FFT vs rpm 映射失败";
        return r;
    }

    const std::string chName = sig.channelName.empty() ? "CH1" : sig.channelName;
    const std::string outBasePath = sig.sourcePath.empty() ? file.path : sig.sourcePath;
    const std::string suffix = kUseOrderTracking
        ? "_" + chName + "_order_vs_rpm.csv"
        : "_" + chName + "_fft_vs_rpm.csv";

    if (job.writeCsvToDisk) {
        r.fftCsvPath = Utils::get_unique_path(outBasePath, suffix);
        if (!exportSvc.WriteRpmSpectrogram(rpmSp, r.fftCsvPath, err)) {
            r.message = err.empty() ? "FFT vs rpm CSV写入失败" : err;
            return r;
        }
    }
    else {
        r.fftCsvPath.clear();
    }

    r.heatmapFreqFrames = BuildFreqFrames(rpmSp);
    r.heatmapAmpFrames = BuildAmpFrames(rpmSp);

    if (r.heatmapFreqFrames.size() != r.heatmapAmpFrames.size()) {
        r.message = "内存热图维度不一致";
        return r;
    }

    r.heatmapIsDb = false;
    r.heatmapXUnit = "Hz";
    r.heatmapYUnit = "RPM";
    r.heatmapZUnit = "Pa";

    r.ok = true;
    r.message = "OK";
    return r;
}