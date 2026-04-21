#include "OctaveFlow.h"

#include "../services/DataReaderService.h"
#include "../services/SpectrumService.h"
#include "../services/ExportService.h"
#include "../features/octave_band/OctaveAnalyzer.h"
#include "../core/Utils.h"

#include <cmath>
#include <vector>
#include <string>

JobResult OctaveFlow::Run(const Job& job, const FileItem& file)
{
    JobResult r;
    r.ok = false;
    r.message = "未开始";

    const bool isOctave1_1 = (job.mode == AnalysisMode::OCTAVE_1_1);
    const bool isOctave1_3 = (job.mode == AnalysisMode::OCTAVE_1_3);
    if (!isOctave1_1 && !isOctave1_3) {
        r.message = "OctaveFlow仅支持OCTAVE_1_1/OCTAVE_1_3";
        return r;
    }

    DataReaderService reader;
    SpectrumService spectrumSvc;
    ExportService exportSvc;

    SignalData sig;
    std::string err;
    if (!reader.ReadSignal(job, file, sig, err)) {
        r.message = err.empty() ? "读取输入信号失败" : err;
        return r;
    }

    const std::vector<double>& x = sig.samples;
    const double fs = sig.fs;
    const std::string chName = sig.channelName.empty() ? "CH1" : sig.channelName;
    const std::string outBasePath = sig.sourcePath.empty() ? file.path : sig.sourcePath;

    // 1) 导出时域信号
    r.timeCsvPath = Utils::get_unique_path(outBasePath, "_" + chName + "_timesignal.csv");
    if (!exportSvc.WriteTimeSignal(x, r.timeCsvPath, err)) {
        r.message = err.empty() ? "时间CSV写入失败" : err;
        return r;
    }

    // 2) 统一由 SpectrumService 做 FFT + 加权
    CVector fft;
    if (!spectrumSvc.ComputeWeightedAveragedFFT(sig, job, fft, err)) {
        if (!err.empty()) r.message = err;
        else if (job.isATFX) r.message = "ATFX FFT失败";
        else if (file.ext == "hdf") r.message = "HDF FFT失败";
        else r.message = "WAV FFT失败";
        return r;
    }

    // 3) 计算 octave
    const size_t nfft = fft.size();
    const size_t nbins = nfft / 2 + 1;
    std::vector<double> avgMag(nbins, 0.0);
    for (size_t k = 0; k < nbins; ++k) {
        avgMag[k] = std::abs(fft[k]);
    }

    const OctaveBandType bandType =
        (job.params.bandsPerOctave == 1) ? OctaveBandType::Full : OctaveBandType::Third;
    const size_t blockSizeForScale = Utils::next_power_of_2(job.params.block_size);

    auto octaveResult = compute_octave_bands_from_fft(
        avgMag, fs, nfft, blockSizeForScale, job.params.window_type,
        bandType, job.params.octaveRefValue, job.params.calibrationFactor
    );

    // 4) 导出 octave（改为 ExportService）
    r.fftCsvPath = Utils::get_unique_path(outBasePath, "_" + chName + "_octave.csv");
    if (!exportSvc.WriteOctave(octaveResult, r.fftCsvPath, err)) {
        r.message = err.empty() ? "倍频程CSV写入失败" : err;
        return r;
    }

    // 5) 峰值：统一走 SpectrumService 通用接口
    if (!octaveResult.bandValues.empty() && !octaveResult.bandCenters.empty()) {
        r.peak = spectrumSvc.CalcPeakFromValues(octaveResult.bandValues, octaveResult.bandCenters);
    }

    // 6) 新增：内存2D结果（供Qt直接绘图）
    r.curveX = octaveResult.bandCenters;
    r.curveY = octaveResult.bandValues;
    r.curveIsDb = true; // octave通常为dB
    r.curveXUnit = "Hz";
    r.curveYUnit = "dB";
    r.curveName = chName + ((job.params.bandsPerOctave == 1) ? "_Octave_1_1" : "_Octave_1_3");

    r.ok = true;
    r.message = "OK";
    return r;
}