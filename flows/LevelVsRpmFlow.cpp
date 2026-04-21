#include "LevelVsRpmFlow.h"

#include "../services/DataReaderService.h"
#include "../services/ExportService.h"
#include "../features/level_vs_time/LevelVsTimeAnalyzer.h"
#include "../features/level_vs_rpm/LevelVsRpmAnalyzer.h"

#include <iostream>
#include <iomanip>
#include <algorithm>
#include <string>

JobResult LevelVsRpmFlow::Run(const Job& job, const FileItem& file)
{
    JobResult r;
    r.ok = false;

    DataReaderService reader;
    ExportService exporter;

    // 1) 读取主信号
    SignalData sig;
    std::string err;
    if (!reader.ReadSignal(job, file, sig, err)) {
        r.message = "读取主信号失败: " + err;
        return r;
    }

    // 2) 读取RPM信号
    std::vector<double> rpm;
    double rpmFs = 0.0;
    if (!reader.ReadRpmSignal(job, file, rpm, rpmFs, err)) {
        r.message = "读取RPM信号失败: " + err;
        return r;
    }

    // 3) 关键修复：按主信号时长重算RPM采样率，避免HDF元数据导致时间轴错位
    //    典型场���：主信号 480000@48k => 10s，RPM 10000点 => rpmFs应为1000Hz
    if (sig.fs > 0.0 && !sig.samples.empty() && !rpm.empty()) {
        const double mainDurationSec = static_cast<double>(sig.samples.size()) / sig.fs;
        if (mainDurationSec > 0.0) {
            const double rpmFsRecalc = static_cast<double>(rpm.size()) / mainDurationSec;
            std::cout << "[DEBUG][LvRpmFlow] rpmFs override by duration: "
                << rpmFs << " -> " << rpmFsRecalc
                << " (rpmN=" << rpm.size()
                << ", mainDurationSec=" << mainDurationSec << ")\n";
            rpmFs = rpmFsRecalc;
        }
    }

    // 4) LEVEL_VS_RPM 参数修正：
    //    - 仅时间计权（禁用频率计权）
    //    - 输出步长不要过大，否则点数太少
    FFTParams p = job.params;
    p.weight_type = Weighting::WeightType::None;

    if (p.level_output_step_sec <= 0.0 || p.level_output_step_sec > 0.02) {
        std::cout << "[DEBUG][LvRpmFlow] override level_output_step_sec: "
            << p.level_output_step_sec << " -> 0.02\n";
        p.level_output_step_sec = 0.02;
    }

    if (p.level_time_constant_sec <= 0.0) p.level_time_constant_sec = 0.125;
    if (p.level_window_sec <= 0.0) p.level_window_sec = p.level_time_constant_sec;

    // 5) 先算 Level vs Time（时间计权级）
    auto levelSeries = LevelVsTimeAnalyzer::Compute(sig.samples, sig.fs, p);

    // 6) 映射到 RPM 并分箱
    const double rpmStep = (job.rpmBinStep > 0.0 ? job.rpmBinStep : 50.0);
    auto lvRpm = LevelVsRpmAnalyzer::ComputeFromLevelSeries(levelSeries, rpm, rpmFs, rpmStep);

    std::cout << std::fixed << std::setprecision(6)
        << "[DEBUG][LvRpmFlow] signalFs=" << sig.fs
        << ", signalSamples=" << sig.samples.size()
        << ", durationSec=" << (sig.fs > 0.0 ? (double)sig.samples.size() / sig.fs : 0.0)
        << ", rpmFs(final)=" << rpmFs
        << ", rpmSamples=" << rpm.size()
        << ", levelPoints=" << levelSeries.points.size()
        << ", rpmBinsOut=" << lvRpm.points.size()
        << ", rpmStep=" << rpmStep
        << ", outputStep=" << p.level_output_step_sec
        << "\n";

    if (lvRpm.points.empty()) {
        r.message = "Level vs RPM结果为空（无有效rpm-bin）";
        return r;
    }

    // 7) 导出CSV（按你当前 ExportService.h 签名）
    const std::string outCsv =
        file.path + "_ch" + std::to_string(job.channelIdx + 1) + "_level_vs_rpm.csv";

    if (!exporter.WriteLevelVsRpm(lvRpm, outCsv, err)) {
        r.message = "导出Level vs RPM失败: " + err;
        return r;
    }

    r.ok = true;
    r.levelVsRpmCsvPath = outCsv;
    r.message = "OK";

    // 峰值预览：取最大level点
    auto it = std::max_element(
        lvRpm.points.begin(),
        lvRpm.points.end(),
        [](const LevelRpmPoint& a, const LevelRpmPoint& b) {
            return a.levelDb < b.levelDb;
        });
    if (it != lvRpm.points.end()) {
        r.peak.freq = it->rpm;    // 复用字段展示rpm
        r.peak.mag = it->levelDb; // dB
    }

    return r;
}