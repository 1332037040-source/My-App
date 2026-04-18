#include "FFTvsRpmFlow.h"

#include "../services/DataReaderService.h"
#include "../services/ExportService.h"
#include "../features/fft_vs_time/FFTvsTimeAnalyzer.h"
#include "../features/fft_vs_rpm/FFTvsRpmMapper.h"
#include "../core/Utils.h"

#include <string>

namespace {
    size_t FindChannelIndexByName(const FileItem& file, const std::string& channelName) {
        for (size_t i = 0; i < file.channels.size(); ++i) {
            if (file.channels[i].channelName == channelName) return i;
        }
        return file.channels.size();
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
        r.message = "FFT vs rpm缺少rpm通道";
        return r;
    }

    DataReaderService reader;
    ExportService exportSvc;

    SignalData sig;
    std::string err;
    if (!reader.ReadSignal(job, file, sig, err)) {
        r.message = err.empty() ? "读取分析通道失败" : err;
        return r;
    }

    const size_t rpmChannelIdx = FindChannelIndexByName(file, job.rpmChannelName);
    if (rpmChannelIdx >= file.channels.size()) {
        r.message = "rpm通道不存在: " + job.rpmChannelName;
        return r;
    }

    Job rpmJob = job;
    rpmJob.channelIdx = rpmChannelIdx;

    SignalData rpmSig;
    if (!reader.ReadSignal(rpmJob, file, rpmSig, err)) {
        r.message = err.empty() ? "读取rpm通道失败" : err;
        return r;
    }

    Spectrogram sp = FFTvsTimeAnalyzer::Compute(sig.samples, sig.fs, job.params);
    if (sp.timeBins == 0 || sp.freqBins == 0 || sp.frameTimeSec.size() != sp.timeBins) {
        r.message = "FFT vs rpm计算失败：时频谱为空";
        return r;
    }

    RpmSpectrogram rpmSp = FFTvsRpmMapper::MapTimeToRpm(
        sp, sp.frameTimeSec, rpmSig.samples, rpmSig.fs, job.rpmBinStep);
    if (rpmSp.rpmBins == 0 || rpmSp.freqBins == 0 || rpmSp.dataDb.empty()) {
        r.message = "FFT vs rpm映射失败：rpm数据无效或为空";
        return r;
    }

    const std::string chName = sig.channelName.empty() ? "CH1" : sig.channelName;
    const std::string outBasePath = sig.sourcePath.empty() ? file.path : sig.sourcePath;

    r.timeCsvPath = Utils::get_unique_path(outBasePath, "_" + chName + "_timesignal.csv");
    if (!exportSvc.WriteTimeSignal(sig.samples, r.timeCsvPath, err)) {
        r.message = err.empty() ? "时间CSV写入失败" : err;
        return r;
    }

    r.fftCsvPath = Utils::get_unique_path(outBasePath, "_" + chName + "_fft_vs_rpm.csv");
    if (!exportSvc.WriteRpmSpectrogram(rpmSp, r.fftCsvPath, err)) {
        r.message = err.empty() ? "转速频谱CSV写入失败" : err;
        return r;
    }

    r.ok = true;
    r.message = "OK";
    return r;
}
