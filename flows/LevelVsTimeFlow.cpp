#include "LevelVsTimeFlow.h"

#include "../services/DataReaderService.h"
#include "../services/ExportService.h"
#include "../features/level_vs_time/LevelVsTimeAnalyzer.h"
#include "../core/Utils.h"

#include <string>

JobResult LevelVsTimeFlow::Run(const Job& job, const FileItem& file)
{
    JobResult r;
    r.ok = false;
    r.message = "未开始";

    if (job.mode != AnalysisMode::LEVEL_VS_TIME) {
        r.message = "LevelVsTimeFlow仅支持AnalysisMode::LEVEL_VS_TIME";
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

    if (sig.samples.empty() || sig.fs <= 0.0) {
        r.message = "输入信号为空或采样率无效";
        return r;
    }

    const std::string chName = sig.channelName.empty() ? "CH1" : sig.channelName;
    const std::string outBasePath = sig.sourcePath.empty() ? file.path : sig.sourcePath;

    LevelVsTimeAnalyzer::LevelSeries series =
        LevelVsTimeAnalyzer::Compute(sig.samples, sig.fs, job.params);

    if (series.points.empty()) {
        if (job.isATFX) r.message = "ATFX Level vs Time失败";
        else if (file.ext == "hdf") r.message = "HDF Level vs Time失败";
        else r.message = "WAV Level vs Time失败";
        return r;
    }

    r.levelVsTimeCsvPath = Utils::get_unique_path(
        outBasePath,
        "_" + chName + "_level_vs_time.csv");

    if (!exportSvc.WriteLevelVsTime(series, r.levelVsTimeCsvPath, err)) {
        r.message = err.empty() ? "Level vs Time CSV写入失败" : err;
        return r;
    }

    r.ok = true;
    r.message = "OK";
    r.peak.freq = 0.0;
    r.peak.mag = series.maxLevelDb;

    return r;
}