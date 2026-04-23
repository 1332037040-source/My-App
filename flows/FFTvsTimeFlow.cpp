#include "FFTvsTimeFlow.h"

#include "../services/DataReaderService.h"
#include "../services/ExportService.h"
#include "../features/fft_vs_time/FFTvsTimeAnalyzer.h"
#include "../core/Utils.h"

#include <string>
#include <vector>
#include <iostream>
#include <iomanip>
#include <algorithm>

namespace
{
    std::vector<std::vector<double>> buildFreqFramesFromSpectrogram(const Spectrogram& sp)
    {
        std::vector<std::vector<double>> freqFrames;
        if (sp.timeBins == 0 || sp.freqBins == 0 || sp.fs <= 0.0 || sp.blockSize == 0) {
            return freqFrames;
        }

        freqFrames.resize(sp.timeBins, std::vector<double>(sp.freqBins, 0.0));
        const double df = sp.fs / static_cast<double>(sp.blockSize);
        for (size_t t = 0; t < sp.timeBins; ++t) {
            for (size_t f = 0; f < sp.freqBins; ++f) {
                freqFrames[t][f] = static_cast<double>(f) * df;
            }
        }
        return freqFrames;
    }

    std::vector<std::vector<double>> buildAmpFramesFromSpectrogramPa(const Spectrogram& sp)
    {
        std::vector<std::vector<double>> ampFrames;
        if (sp.timeBins == 0 || sp.freqBins == 0 || sp.dataLinear.empty()) {
            return ampFrames;
        }

        ampFrames.resize(sp.timeBins, std::vector<double>(sp.freqBins, 0.0));
        for (size_t t = 0; t < sp.timeBins; ++t) {
            for (size_t f = 0; f < sp.freqBins; ++f) {
                ampFrames[t][f] = sp.atLinear(t, f);
            }
        }
        return ampFrames;
    }
}

JobResult FFTvsTimeFlow::Run(const Job& job, const FileItem& file)
{
    JobResult r;
    r.ok = false;
    r.message = "未开始";

    if (job.mode != AnalysisMode::FFT_VS_TIME) {
        r.message = "FFTvsTimeFlow仅支持AnalysisMode::FFT_VS_TIME";
        return r;
    }

    DataReaderService reader;
    ExportService exportSvc;

    SignalData sig;
    std::string err;
    if (!reader.ReadSignal(job, file, sig, err)) {
        r.message = err.empty() ? "读取输入信号失败" : err;
        return r;
    }

    const std::string chName = sig.channelName.empty() ? "CH1" : sig.channelName;
    const std::string outBasePath = sig.sourcePath.empty() ? file.path : sig.sourcePath;

    if (job.writeCsvToDisk) {
        r.timeCsvPath = Utils::get_unique_path(outBasePath, "_" + chName + "_timesignal.csv");
        if (!exportSvc.WriteTimeSignal(sig.samples, r.timeCsvPath, err)) {
            r.message = err.empty() ? "时间CSV写入失败" : err;
            return r;
        }
    }
    else {
        r.timeCsvPath.clear();
    }

    Spectrogram sp = FFTvsTimeAnalyzer::Compute(sig.samples, sig.fs, job.params);
    if (sp.timeBins == 0 || sp.freqBins == 0 || sp.dataLinear.empty()) {
        if (job.isATFX) r.message = "ATFX FFT vs time失败";
        else if (file.ext == "hdf") r.message = "HDF FFT vs time失败";
        else r.message = "WAV FFT vs time失败";
        return r;
    }

    if (job.writeCsvToDisk) {
        r.fftCsvPath = Utils::get_unique_path(outBasePath, "_" + chName + "_fft_vs_time.csv");
        if (!exportSvc.WriteSpectrogram(sp, r.fftCsvPath, err)) {
            r.message = err.empty() ? "时频CSV写入失败" : err;
            return r;
        }
    }
    else {
        r.fftCsvPath.clear();
    }

    r.heatmapFreqFrames = buildFreqFramesFromSpectrogram(sp);
    r.heatmapAmpFrames = buildAmpFramesFromSpectrogramPa(sp);

    if (r.heatmapFreqFrames.size() != r.heatmapAmpFrames.size()) {
        r.message = "内存热图维度不一致";
        return r;
    }

    r.heatmapIsDb = false;
    r.heatmapXUnit = "Hz";
    r.heatmapYUnit = "Time";
    r.heatmapZUnit = "Pa";

    r.ok = true;
    r.message = "OK";
    return r;
}