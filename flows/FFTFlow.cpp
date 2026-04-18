#include "FFTFlow.h"

#include "../services/DataReaderService.h"
#include "../services/SpectrumService.h"
#include "../services/ExportService.h"
#include "../core/Utils.h"

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

    // 1) 读取信号
    SignalData sig;
    std::string err;
    if (!dataSvc.ReadSignal(job, file, sig, err)) {
        r.message = err.empty() ? "读取输入信号失败" : err;
        return r;
    }

    const std::string chName = sig.channelName.empty() ? "CH1" : sig.channelName;
    const std::string outBasePath = sig.sourcePath.empty() ? file.path : sig.sourcePath;

    // 2) 写时域 CSV
    r.timeCsvPath = Utils::get_unique_path(outBasePath, "_" + chName + "_timesignal.csv");
    if (!exportSvc.WriteTimeSignal(sig.samples, r.timeCsvPath, err)) {
        r.message = err.empty() ? "时间CSV写入失败" : err;
        return r;
    }

    // 3) 计算 FFT
    CVector fft;
    if (!spectrumSvc.ComputeAveragedFFT(sig, job, fft, err)) {
        r.message = err.empty() ? "FFT计算失败" : err;
        return r;
    }

    // 4) 加权
    spectrumSvc.ApplyWeighting(fft, sig.fs, job);

    // 5) 导出 FFT CSV
    r.fftCsvPath = Utils::get_unique_path(outBasePath, "_" + chName + "_fft_result.csv");
    if (!exportSvc.WriteFFT(fft, sig.fs, r.fftCsvPath, err)) {
        r.message = err.empty() ? "FFT CSV写入失败" : err;
        return r;
    }

    // 6) 峰值
    r.peak = spectrumSvc.CalcPeakFromFFT(fft, sig.fs);

    r.ok = true;
    r.message = "OK";
    return r;
}