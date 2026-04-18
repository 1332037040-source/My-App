#include <iostream>
#include <thread>
#include <algorithm>
#include <iomanip>
#include <cmath>
#include <string>
#include <vector>

#include "planner/TaskPlanner.h"
#include "planner/TaskBuilder.h"
#include "report/ReportWriter.h"
#include "domain/ParseUtils.h"
#include "engine/Engine.h"

#include "app/RunRequest.h"
#include "app/RunRequestMapper.h"
#include "core_api/CoreAnalysisFacade.h"

static const char* ModeToText(AnalysisMode mode)
{
    switch (mode) {
    case AnalysisMode::FFT: return "FFT";
    case AnalysisMode::FFT_VS_TIME: return "FFT_VS_TIME";
    case AnalysisMode::FFT_VS_RPM: return "FFT_VS_RPM";
    case AnalysisMode::OCTAVE_1_1: return "OCTAVE_1_1";
    case AnalysisMode::OCTAVE_1_3: return "OCTAVE_1_3";
    case AnalysisMode::LEVEL_VS_TIME: return "LEVEL_VS_TIME";
    default: return "UNKNOWN";
    }
}

static bool IsHdfExt(const std::string& ext)
{
    return ext == "hdf" || ext == "h5" || ext == "hdf5";
}

static void PrintResults(
    const std::vector<JobResult>& results,
    const std::vector<Job>& jobs,
    const std::vector<FileItem>& files,
    ReportWriter& reporter
) {
    std::cout << "\n\n";
    std::cout << "============= 峰值预览结果 =============\n";
    std::cout.flush();

    int success = 0;
    for (size_t i = 0; i < results.size(); ++i) {
        const auto& r = results[i];
        const auto& job = jobs[i];
        const auto& file = files[job.fileIdx];

        std::cout << "[任务 " << (i + 1) << "] ";
        if (job.isATFX) {
            std::cout << "ATFX | " << file.path
                << " | 通道." << file.channels[job.channelIdx].channelName;
        }
        else if (IsHdfExt(file.ext)) {
            std::cout << "HDF | " << file.path
                << " | 通��." << file.channels[job.channelIdx].channelName;
        }
        else {
            std::cout << "WAV | " << file.path;
        }
        std::cout << " | 模式=" << ModeToText(job.mode) << "\n";

        if (!r.ok) {
            std::cout << "  [状态] 失败: " << r.message << "\n";
        }
        else {
            if (!r.levelVsTimeCsvPath.empty()) {
                std::cout << "  [结果] Level vs Time CSV: " << r.levelVsTimeCsvPath << "\n";
                std::cout << std::fixed << std::setprecision(3);
                std::cout << "  [最大级值] " << r.peak.mag << " dB\n";
            }
            else if (std::abs(r.peak.mag) > 1e-12) {
                std::cout << std::fixed << std::setprecision(3);
                std::cout << "  [峰值] 频率=" << r.peak.freq << " Hz"
                    << " | 幅值=" << r.peak.mag << "\n";
            }
            else {
                std::cout << "  [峰值] 未计算(无有效幅值)\n";
            }
            success++;
        }
        std::cout.flush();
    }

    std::cout << "========================================\n";
    std::cout.flush();
    reporter.PrintSummary(success, static_cast<int>(jobs.size()));
}

int main(int argc, char* argv[]) {
    std::vector<std::string> inputPaths;
    std::vector<FileItem> files;
    std::vector<Job> jobs;

    TaskPlanner planner;
    ReportWriter reporter;
    FFT11Engine engine;

    std::cout << "======= FFT峰值校准模式 + 引擎V2 =======\n";

    const bool noUiMode = (argc >= 2 && std::string(argv[1]) == "--no-ui");
    const bool coreApiTestMode = (argc >= 2 && std::string(argv[1]) == "--core-api-test");

    if (coreApiTestMode) {
        if (argc < 4) {
            std::cerr << "[错误] --core-api-test 模式需要至少两个参数：<filePath> <outputDir>\n";
            return 1;
        }

        CoreAnalysisFacade facade;

        CoreAnalysisRequest req;
        req.filePath = argv[2];
        req.outputDir = argv[3];
        req.analysisMode = "fft";
        req.fftSize = 8192;
        req.overlap = 0.5;
        req.weighting = "Z";

        CoreAnalysisResult res = facade.run(req);

        std::cout << "\n======= Core API Test Result =======\n";
        std::cout << "success        : " << (res.success ? "true" : "false") << "\n";
        std::cout << "message        : " << res.message << "\n";
        std::cout << "timeSignalCsv  : " << res.timeSignalCsv << "\n";
        std::cout << "fftCsv         : " << res.fftCsv << "\n";
        std::cout << "spectrogramCsv : " << res.spectrogramCsv << "\n";
        std::cout << "octaveCsv      : " << res.octaveCsv << "\n";
        std::cout << "levelVsTimeCsv : " << res.levelVsTimeCsv << "\n";
        std::cout << "reportFile     : " << res.reportFile << "\n";
        std::cout << "peakFrequency  : " << res.peakFrequency << "\n";
        std::cout << "peakValue      : " << res.peakValue << "\n";
        std::cout << "generatedFiles : " << res.generatedFiles.size() << "\n";

        for (size_t i = 0; i < res.generatedFiles.size(); ++i) {
            std::cout << "  [" << i << "] " << res.generatedFiles[i] << "\n";
        }

        std::cout << "====================================\n";
        return res.success ? 0 : 2;
    }

    if (noUiMode) {
        if (argc < 3) {
            std::cerr << "[错误] --no-ui 模式至少需要一个文件路径\n";
            return 1;
        }

        RunRequest runReq;
        runReq.mode = AnalysisMode::FFT;
        runReq.rpmBinStep = 50.0;
        runReq.maxThreads = 0;
        runReq.maxRetries = 1;
        runReq.enableCancel = false;

        runReq.fftParams.block_size = 8192;
        runReq.fftParams.overlap_ratio = 0.5;
        runReq.fftParams.window_type = Window::WindowType::Hanning;
        runReq.fftParams.amp_scaling = Analyzer::AmplitudeScaling::Peak;
        runReq.fftParams.weight_type = Weighting::WeightType::None;

        for (int i = 2; i < argc; ++i) {
            runReq.inputPaths.push_back(argv[i]);
        }

        auto mapped = MapRunRequest(runReq);

        auto built = TaskBuilder::BuildFromRequest(mapped.buildReq);
        if (!built.ok) {
            std::cerr << "[错误] TaskBuilder 失败: " << built.message << "\n";
            return 1;
        }

        files = std::move(built.files);
        jobs = std::move(built.jobs);

        reporter.PrintPlan(jobs, files);

        mapped.engineCfg.files = files;
        mapped.engineCfg.jobs = jobs;

        auto runRet = engine.Run(
            mapped.engineCfg,
            [&](const EngineProgress& p) { (void)p; },
            [&](const std::string& msg) { std::cout << msg << "\n"; },
            nullptr
        );

        PrintResults(runRet.results, jobs, files, reporter);
        std::cout << "[完成] --no-ui 模式执行完毕\n";
        return 0;
    }

    if (!planner.CollectInputPaths(inputPaths)) {
        std::cerr << "[错误] 未获取文件路径\n";
        return 1;
    }
    if (!planner.BuildFileItems(inputPaths, files)) {
        std::cerr << "[错误] 构建文件列表失败\n";
        return 1;
    }
    if (!planner.SelectFiles(files)) {
        std::cerr << "[错误] 文件选择失败\n";
        return 1;
    }
    if (!planner.LoadAndSelectChannels(files)) {
        std::cerr << "[错误] 通道加载/选择失败\n";
        return 1;
    }
    if (!planner.ConfigureParamsAndBuildJobs(files, jobs)) {
        std::cerr << "[错误] 参数配置与任务构建失败\n";
        return 1;
    }

    reporter.PrintPlan(jobs, files);

    EngineRunConfig cfg{};
    {
        unsigned int hw = std::thread::hardware_concurrency();
        if (hw == 0) hw = 4;

        std::cout << "指定线程数=自动，默认" << hw << "个: ";
        std::string s;
        std::getline(std::cin, s);
        s = trim_copy(s);
        if (!s.empty()) {
            try {
                size_t v = static_cast<size_t>(std::stoull(s));
                if (v > 0) cfg.maxThreads = v;
            }
            catch (...) {
                std::cout << "[提示] 线程数无效，使用自动值\n";
            }
        }

        std::cout << "失败重试次数=1次: ";
        std::getline(std::cin, s);
        s = trim_copy(s);
        if (!s.empty()) {
            try {
                int r = std::stoi(s);
                if (r >= 0 && r <= 5) cfg.maxRetries = r;
            }
            catch (...) {}
        }

        std::cout << "是否允许取消? (y/n, 默认y): ";
        std::getline(std::cin, s);
        s = trim_copy(s);
        std::transform(s.begin(), s.end(), s.begin(), ::tolower);
        cfg.enableCancel = (s.empty() || s == "y" || s == "yes" || s == "1");
    }

    cfg.files = files;
    cfg.jobs = jobs;

    auto runRet = engine.Run(
        cfg,
        [&](const EngineProgress& p) { (void)p; },
        [&](const std::string& msg) { std::cout << msg << "\n"; },
        nullptr
    );

    PrintResults(runRet.results, jobs, files, reporter);

    std::cout << "[完成] 程序执行完毕\n";
    return 0;
}
