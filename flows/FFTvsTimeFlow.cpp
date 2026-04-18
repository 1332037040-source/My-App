#include "FFTvsTimeFlow.h"

#include "../services/DataReaderService.h"
#include "../services/ExportService.h"
#include "../features/fft_vs_time/FFTvsTimeAnalyzer.h"
#include "../core/Utils.h"

#include <string>

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

    r.timeCsvPath = Utils::get_unique_path(outBasePath, "_" + chName + "_timesignal.csv");
    if (!exportSvc.WriteTimeSignal(sig.samples, r.timeCsvPath, err)) {
        r.message = err.empty() ? "时间CSV写入失败" : err;
        return r;
    }

    Spectrogram sp = FFTvsTimeAnalyzer::Compute(sig.samples, sig.fs, job.params);
    if (sp.timeBins == 0 || sp.freqBins == 0) {
        if (job.isATFX) r.message = "ATFX FFT vs time失败";
        else if (file.ext == "hdf") r.message = "HDF FFT vs time失败";
        else r.message = "WAV FFT vs time失败";
        return r;
    }

    r.fftCsvPath = Utils::get_unique_path(outBasePath, "_" + chName + "_fft_vs_time.csv");
    if (!exportSvc.WriteSpectrogram(sp, r.fftCsvPath, err)) {
        r.message = err.empty() ? "时频CSV写入失败" : err;
        return r;
    }

    r.ok = true;
    r.message = "OK";
    return r;
}