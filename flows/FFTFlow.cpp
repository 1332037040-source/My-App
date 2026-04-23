#include "FFTFlow.h"

#include "../services/DataReaderService.h"
#include "../services/SpectrumService.h"
#include "../services/ExportService.h"
#include "../core/Utils.h"

#include <cmath>
#include <string>
#include <iostream>
#include <iomanip>
#include <algorithm>

JobResult FFTFlow::Run(const Job& job, const FileItem& file)
{
    JobResult r;
    r.ok = false;
    r.message = "未开始";

    if (job.mode != AnalysisMode::FFT) {
        r.message = "FFTFlow仅支持AnalysisMode::FFT";
        return r;
    }

    DataReaderService dataSvc;
    SpectrumService spectrumSvc;
    ExportService exportSvc;

    SignalData sig;
    std::string err;
    if (!dataSvc.ReadSignal(job, file, sig, err)) {
        r.message = err.empty() ? "读取输入信号失败" : err;
        return r;
    }

    const std::string chName = sig.channelName.empty() ? "CH1" : sig.channelName;
    const std::string outBasePath = sig.sourcePath.empty() ? file.path : sig.sourcePath;

    // 2) 写时域 CSV（按开关）
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

    CVector fft;
    if (!spectrumSvc.ComputeAveragedFFT(sig, job, fft, err)) {
        r.message = err.empty() ? "FFT计算失败" : err;
        return r;
    }

    spectrumSvc.ApplyWeighting(fft, sig.fs, job);

    // 5) 导出 FFT CSV（按开关）
    if (job.writeCsvToDisk) {
        r.fftCsvPath = Utils::get_unique_path(outBasePath, "_" + chName + "_fft_result.csv");
        if (!exportSvc.WriteFFT(fft, sig.fs, r.fftCsvPath, err)) {
            r.message = err.empty() ? "FFT CSV写入失败" : err;
            return r;
        }
    }
    else {
        r.fftCsvPath.clear();
    }

    r.peak = spectrumSvc.CalcPeakFromFFT(fft, sig.fs);

    {
        const size_t nfft = fft.size();
        const size_t nbins = (nfft > 0) ? (nfft / 2 + 1) : 0;

        r.curveX.clear();
        r.curveY.clear();
        r.curveX.reserve(nbins);
        r.curveY.reserve(nbins);

        for (size_t k = 0; k < nbins; ++k) {
            const double f = (sig.fs > 0.0 && nfft > 0)
                ? (static_cast<double>(k) * sig.fs / static_cast<double>(nfft))
                : 0.0;
            const double mag = std::abs(fft[k]);
            r.curveX.push_back(f);
            r.curveY.push_back(mag);
        }

        r.curveIsDb = false;
        r.curveXUnit = "Hz";
        r.curveYUnit = "Pa";
        r.curveName = chName + "_FFT_Pa";
    }

    r.ok = true;
    r.message = "OK";
    return r;
}