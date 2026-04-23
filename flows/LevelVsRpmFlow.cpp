#include "LevelVsRpmFlow.h"

#include "../services/DataReaderService.h"
#include "../services/ExportService.h"
#include "../features/level_vs_rpm/LevelVsRpmAnalyzer.h"

#include <iostream>
#include <iomanip>
#include <algorithm>
#include <string>
#include <cmath>

JobResult LevelVsRpmFlow::Run(const Job& job, const FileItem& file)
{
    JobResult r;
    r.ok = false;

    DataReaderService reader;
    ExportService exporter;

    SignalData sig;
    std::string err;
    if (!reader.ReadSignal(job, file, sig, err)) {
        r.message = "读取主信号失败: " + err;
        return r;
    }

    std::vector<double> rpm;
    double rpmFs = 0.0;
    if (!reader.ReadRpmSignal(job, file, rpm, rpmFs, err)) {
        r.message = "读取RPM信号失败: " + err;
        return r;
    }

    if (sig.fs > 0.0 && !sig.samples.empty() && !rpm.empty()) {
        const double mainDurationSec = static_cast<double>(sig.samples.size()) / sig.fs;
        if (mainDurationSec > 0.0) {
            const double rpmFsRecalc = static_cast<double>(rpm.size()) / mainDurationSec;
            rpmFs = rpmFsRecalc;
        }
    }

    FFTParams p = job.params;

    if (p.level_output_step_sec <= 0.0 || p.level_output_step_sec > 0.02) {
        p.level_output_step_sec = 0.02;
    }

    if (p.level_time_constant_sec <= 0.0) p.level_time_constant_sec = 0.125;
    if (p.level_window_sec <= 0.0) p.level_window_sec = p.level_time_constant_sec;

    const double rpmStep = (job.rpmBinStep > 0.0 ? job.rpmBinStep : 50.0);
    auto lvRpm = LevelVsRpmAnalyzer::Compute(
        sig.samples, sig.fs, p, rpm, rpmFs, rpmStep);

    if (lvRpm.points.empty()) {
        r.message = "Level vs RPM结果为空（无有效rpm-bin）";
        return r;
    }

    const std::string outCsv =
        file.path + "_ch" + std::to_string(job.channelIdx + 1) + "_level_vs_rpm.csv";

    if (job.writeCsvToDisk) {
        if (!exporter.WriteLevelVsRpm(lvRpm, outCsv, err)) {
            r.message = "导出Level vs RPM失败: " + err;
            return r;
        }
        r.levelVsRpmCsvPath = outCsv;
    }
    else {
        r.levelVsRpmCsvPath.clear();
    }

    r.curveX.clear();
    r.curveY.clear();
    r.curveX.reserve(lvRpm.points.size());
    r.curveY.reserve(lvRpm.points.size());

    for (const auto& pnt : lvRpm.points) {
        r.curveX.push_back(pnt.rpm);
        r.curveY.push_back(pnt.levelPa);
    }

    r.curveIsDb = false;
    r.curveXUnit = "rpm";
    r.curveYUnit = "Pa";
    r.curveName = "Level vs RPM (Pa)";

    r.ok = true;
    r.message = "OK";

    auto it = std::max_element(
        lvRpm.points.begin(),
        lvRpm.points.end(),
        [](const LevelRpmPoint& a, const LevelRpmPoint& b) {
            return a.levelPa < b.levelPa;
        });
    if (it != lvRpm.points.end()) {
        r.peak.freq = it->rpm;
        r.peak.mag = it->levelPa;
    }

    return r;
}