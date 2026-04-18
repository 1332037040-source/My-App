#include "TaskPlanner.h"
#include "../domain/ParseUtils.h"
#include "io/ATFXReader.h"
#include "io/HDFReader.h"
#include "../domain/Types.h"
#include <iostream>
#include <set>
#include <map>
#include <iomanip>
#include <cmath>
#include <sstream>
#include <algorithm>
#include <cctype>

namespace {
    static std::string trim_ws(const std::string& s) {
        size_t b = 0, e = s.size();
        while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
        while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
        return s.substr(b, e - b);
    }
    static std::string trim_copy(const std::string& s) { return trim_ws(s); }

    static std::string lower_copy(std::string s) {
        std::transform(s.begin(), s.end(), s.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    }

    static std::vector<size_t> parse_indices_csv_1based_safe(const std::string& raw, size_t maxCount) {
        std::set<size_t> uniq;
        if (maxCount == 0) return {};
        std::string s = trim_ws(raw);
        std::string low = lower_copy(s);
        if (s.empty() || low == "all") {
            for (size_t i = 0; i < maxCount; ++i) uniq.insert(i);
            return std::vector<size_t>(uniq.begin(), uniq.end());
        }

        long long cur = 0;
        bool inNum = false;
        auto flush_num = [&]() {
            if (!inNum) return;
            if (cur >= 1 && static_cast<size_t>(cur) <= maxCount) {
                uniq.insert(static_cast<size_t>(cur - 1));
            }
            cur = 0;
            inNum = false;
            };

        for (size_t i = 0; i < s.size(); ++i) {
            unsigned char ch = static_cast<unsigned char>(s[i]);
            if (std::isdigit(ch)) {
                inNum = true;
                cur = cur * 10 + (ch - '0');
            }
            else {
                flush_num();
            }
        }
        flush_num();

        return std::vector<size_t>(uniq.begin(), uniq.end());
    }

    static ATFXChannelInfo ToATFXLike(const HDFChannelInfo& h) {
        ATFXChannelInfo a;
        a.channelName = h.channelName;
        a.channelLabel = h.channelLabel.empty() ? h.channelName : h.channelLabel;
        a.dataType = h.dataType;
        a.dataLength = h.dataLength;
        a.dataOffset = h.dataOffset;
        a.unit = h.unit;
        a.dof = h.dof.empty() ? "-" : h.dof;
        return a;
    }

    static void AskLevelVsTimeParams(FFTParams& p, const std::string& title) {
        std::cout << std::fixed << std::setprecision(9)
            << "\n========== [DEBUG] AskLevelVsTimeParams BEGIN ==========\n"
            << "title                  = " << title << "\n"
            << "initial time_weighting = " << p.time_weighting << "\n"
            << "initial time_constant  = " << p.level_time_constant_sec << "\n"
            << "initial window_sec     = " << p.level_window_sec << "\n"
            << "initial output_step    = " << p.level_output_step_sec << "\n"
            << "initial calibration    = " << p.calibrationFactor << "\n"
            << "initial ref            = " << p.octaveRefValue << "\n"
            << "========================================================\n"
            << std::endl;

        std::cout << "\n====================================\n";
        std::cout << title << "\n";
        std::cout << "Level vs Time 参数设置\n";
        std::cout << "====================================\n";

        std::string s;

        std::cout << "[1/6] 频率计权 (0=Z, 1=A, 2=C) [默认 0]: ";
        std::getline(std::cin, s);
        s = trim_ws(s);
        if (s == "1") p.weight_type = Weighting::WeightType::A;
        else if (s == "2") p.weight_type = Weighting::WeightType::C;
        else p.weight_type = Weighting::WeightType::None;

        std::cout << "[2/6] 时间计权 (1=Fast, 2=Slow, 3=Impulse, 4=Rectangle, 5=Manual) [默认 1]: ";
        std::getline(std::cin, s);
        s = trim_ws(s);
        if (s == "2") p.time_weighting = "slow";
        else if (s == "3") p.time_weighting = "impulse";
        else if (s == "4") p.time_weighting = "rectangle";
        else if (s == "5") p.time_weighting = "manual";
        else p.time_weighting = "fast";

        // 时间常数 / 窗长
        if (p.time_weighting == "manual") {
            std::cout << "[3/6] 时间常数(秒) [默认 0.125]: ";
            std::getline(std::cin, s);
            s = trim_ws(s);
            if (!s.empty()) {
                try {
                    double v = std::stod(s);
                    if (v > 0.0) p.level_time_constant_sec = v;
                }
                catch (...) {}
            }
            if (p.level_time_constant_sec <= 0.0) {
                p.level_time_constant_sec = 0.125;
            }
            p.level_window_sec = p.level_time_constant_sec;
        }
        else if (p.time_weighting == "rectangle") {
            std::cout << "[3/6] 矩形窗长(秒) [默认 0.125]: ";
            std::getline(std::cin, s);
            s = trim_ws(s);
            if (!s.empty()) {
                try {
                    double v = std::stod(s);
                    if (v > 0.0) p.level_window_sec = v;
                }
                catch (...) {}
            }
            if (p.level_window_sec <= 0.0) {
                p.level_window_sec = 0.125;
            }
            p.level_time_constant_sec = p.level_window_sec;
        }
        else if (p.time_weighting == "fast") {
            p.level_time_constant_sec = 0.125;
            p.level_window_sec = 0.125;
        }
        else if (p.time_weighting == "slow") {
            p.level_time_constant_sec = 1.0;
            p.level_window_sec = 1.0;
        }
        else if (p.time_weighting == "impulse") {
            p.level_time_constant_sec = 0.035;
            p.level_window_sec = 0.035;
        }
        else {
            p.level_time_constant_sec = 0.125;
            p.level_window_sec = 0.125;
        }

        // 默认输出步长：与时间计权绑定
        double defaultOutputStepSec = 0.005333333;

        if (p.time_weighting == "slow") {
            defaultOutputStepSec = 0.042666667;
        }
        else if (p.time_weighting == "impulse") {
            defaultOutputStepSec = 0.001333333;
        }
        else if (p.time_weighting == "rectangle") {
            defaultOutputStepSec = 0.005333333;
        }
        else if (p.time_weighting == "manual") {
            defaultOutputStepSec = std::max(p.level_time_constant_sec / 23.4375, 0.001);
        }
        else {
            defaultOutputStepSec = 0.005333333;
        }

        std::cout << "[DEBUG] computed defaultOutputStepSec = " << defaultOutputStepSec << std::endl;

        std::cout << "[4/6] 输出步长/降采样步长(秒) [默认 " << defaultOutputStepSec << "]: ";
        std::getline(std::cin, s);
        s = trim_ws(s);
        if (!s.empty()) {
            try {
                double v = std::stod(s);
                if (v > 0.0) p.level_output_step_sec = v;
            }
            catch (...) {}
        }
        else {
            p.level_output_step_sec = defaultOutputStepSec;
        }

        std::cout << "[5/6] 校准因子 [默认 1.0]: ";
        std::getline(std::cin, s);
        s = trim_ws(s);
        if (!s.empty()) {
            try {
                double v = std::stod(s);
                if (v > 0.0) p.calibrationFactor = v;
            }
            catch (...) {}
        }
        else if (p.calibrationFactor <= 0.0) {
            p.calibrationFactor = 1.0;
        }

        std::cout << "[6/6] 参考值(默认 20e-6): ";
        std::getline(std::cin, s);
        s = trim_ws(s);
        if (!s.empty()) {
            try {
                double v = std::stod(s);
                if (v > 0.0) p.octaveRefValue = v;
            }
            catch (...) {}
        }
        else if (p.octaveRefValue <= 0.0) {
            p.octaveRefValue = 20e-6;
        }

        std::cout << std::fixed << std::setprecision(9)
            << "\n========== [DEBUG] AskLevelVsTimeParams END ==========\n"
            << "final time_weighting   = " << p.time_weighting << "\n"
            << "final time_constant    = " << p.level_time_constant_sec << "\n"
            << "final window_sec       = " << p.level_window_sec << "\n"
            << "final output_step      = " << p.level_output_step_sec << "\n"
            << "final calibration      = " << p.calibrationFactor << "\n"
            << "final ref              = " << p.octaveRefValue << "\n"
            << "======================================================\n"
            << std::endl;
    }

    static void ApplyModeSpecificParams(
        FFTParams& p,
        AnalysisMode selectedAnalysisMode,
        int bandsPerOctave,
        OctaveMethod octaveMethod,
        double octaveRefValue,
        const std::string& title)
    {
        if (selectedAnalysisMode == AnalysisMode::OCTAVE_1_1 || selectedAnalysisMode == AnalysisMode::OCTAVE_1_3) {
            p.bandsPerOctave = bandsPerOctave;
            p.octaveMethod = octaveMethod;
            p.octaveRefValue = octaveRefValue;
        }

        if (selectedAnalysisMode == AnalysisMode::LEVEL_VS_TIME) {
            AskLevelVsTimeParams(p, title);
        }
    }
} // namespace

bool TaskPlanner::CollectInputPaths(std::vector<std::string>& inputPaths) {
    inputPaths.clear();
    std::cout << "请输入 WAV 或 ATFX 或 HDF 文件路径（每行一个，输入 end 结束）:\n";
    std::string s;
    while (true) {
        std::cout << "> ";
        std::getline(std::cin, s);
        s = trim_ws(s);
        if (s.empty() || s == "end") break;
        if (s.size() >= 2 && s.front() == '"' && s.back() == '"') {
            s = s.substr(1, s.size() - 2);
        }
        inputPaths.push_back(s);
    }
    return !inputPaths.empty();
}

bool TaskPlanner::BuildFileItems(const std::vector<std::string>& inputPaths, std::vector<FileItem>& files) {
    files.clear();
    files.reserve(inputPaths.size());
    for (const auto& p : inputPaths) {
        FileItem f;
        f.path = p;
        f.ext = get_ext_lower(p);
        f.selected = true;
        files.push_back(f);
    }
    return !files.empty();
}

bool TaskPlanner::SelectFiles(std::vector<FileItem>& files) {
    std::cout << "\n文件列表：\n";
    for (size_t i = 0; i < files.size(); ++i) {
        std::cout << "[" << (i + 1) << "] " << files[i].path << " (" << files[i].ext << ")\n";
    }
    std::cout << "请选择要分析的文件（如 1,2,3；all=全部；回车=全部）: ";
    std::string s;
    std::getline(std::cin, s);
    auto idx = parse_indices_csv_1based_safe(s, files.size());
    if (idx.empty()) {
        for (size_t i = 0; i < files.size(); ++i) idx.push_back(i);
    }
    std::set<size_t> chosen(idx.begin(), idx.end());
    for (size_t i = 0; i < files.size(); ++i) {
        files[i].selected = (chosen.count(i) > 0);
    }
    return true;
}

bool TaskPlanner::LoadAndSelectChannels(std::vector<FileItem>& files) {
    for (size_t fi = 0; fi < files.size(); ++fi) {
        if (!files[fi].selected) continue;

        if (files[fi].ext == "atfx") {
            FFT11_ATFXReader atfx;
            if (!atfx.GetAllChannels(files[fi].path, files[fi].channels, files[fi].fs) || files[fi].channels.empty()) {
                std::cerr << "[错误] 无法读取 ATFX 通道: " << files[fi].path << "\n";
                files[fi].selected = false;
                continue;
            }

            std::cout << "\n>>> 文件[" << (fi + 1) << "] " << files[fi].path << "\n";
            std::cout << std::left
                << std::setw(6) << "ID"
                << std::setw(26) << "Name"
                << std::setw(10) << "Unit"
                << std::setw(10) << "F (Hz)"
                << std::setw(8) << "Dof"
                << "\n";

            for (size_t ci = 0; ci < files[fi].channels.size(); ++ci) {
                const auto& ch = files[fi].channels[ci];
                double fHz = files[fi].fs;
                std::cout << std::left
                    << std::setw(6) << ("[" + std::to_string(ci + 1) + "]")
                    << std::setw(26) << ch.channelName
                    << std::setw(10) << (ch.unit.empty() ? "-" : ch.unit)
                    << std::setw(10) << static_cast<int>(std::round(fHz))
                    << std::setw(8) << (ch.dof.empty() ? "-" : ch.dof)
                    << "\n";
            }

            std::cout << "请选择该文件通道（如 1,3,5；all=全部；回车=全部）: ";
            std::string s;
            std::getline(std::cin, s);
            s = trim_ws(s);

            files[fi].selectedChannels = parse_indices_csv_1based_safe(s, files[fi].channels.size());
            if (files[fi].selectedChannels.empty()) {
                for (size_t ci = 0; ci < files[fi].channels.size(); ++ci) files[fi].selectedChannels.push_back(ci);
            }

            std::cout << "[DBG] 文件[" << (fi + 1) << "] 选中通道数: "
                << files[fi].selectedChannels.size() << " -> ";
            for (size_t k = 0; k < files[fi].selectedChannels.size(); ++k) {
                std::cout << (files[fi].selectedChannels[k] + 1);
                if (k + 1 < files[fi].selectedChannels.size()) std::cout << ",";
            }
            std::cout << "\n";
        }
        else if (files[fi].ext == "hdf") {
            FFT11_HDFReader hdf;
            std::vector<HDFChannelInfo> hdfChannels;
            double fs = 0.0;

            if (!hdf.GetAllChannels(files[fi].path, hdfChannels, fs) || hdfChannels.empty()) {
                std::cerr << "[错误] 无法读取 HDF 通道: " << files[fi].path << "\n";
                files[fi].selected = false;
                continue;
            }

            files[fi].channels.clear();
            files[fi].channels.reserve(hdfChannels.size());
            for (const auto& hc : hdfChannels) {
                files[fi].channels.push_back(ToATFXLike(hc));
            }
            files[fi].fs = fs;

            std::cout << "\n>>> 文件[" << (fi + 1) << "] " << files[fi].path << " [HDF]\n";
            std::cout << std::left
                << std::setw(6) << "ID"
                << std::setw(26) << "Name"
                << std::setw(10) << "Unit"
                << std::setw(10) << "F (Hz)"
                << std::setw(8) << "Dof"
                << "\n";

            for (size_t ci = 0; ci < files[fi].channels.size(); ++ci) {
                const auto& ch = files[fi].channels[ci];
                double fHz = files[fi].fs;
                std::cout << std::left
                    << std::setw(6) << ("[" + std::to_string(ci + 1) + "]")
                    << std::setw(26) << ch.channelName
                    << std::setw(10) << (ch.unit.empty() ? "-" : ch.unit)
                    << std::setw(10) << static_cast<int>(std::round(fHz))
                    << std::setw(8) << (ch.dof.empty() ? "-" : ch.dof)
                    << "\n";
            }

            std::cout << "请选择该文件通道（如 1,3,5；all=全部；回车=全部）: ";
            std::string s;
            std::getline(std::cin, s);
            s = trim_ws(s);

            files[fi].selectedChannels = parse_indices_csv_1based_safe(s, files[fi].channels.size());
            if (files[fi].selectedChannels.empty()) {
                for (size_t ci = 0; ci < files[fi].channels.size(); ++ci) files[fi].selectedChannels.push_back(ci);
            }

            std::cout << "[DBG] 文件[" << (fi + 1) << "] 选中HDF通道数: "
                << files[fi].selectedChannels.size() << " -> ";
            for (size_t k = 0; k < files[fi].selectedChannels.size(); ++k) {
                std::cout << (files[fi].selectedChannels[k] + 1);
                if (k + 1 < files[fi].selectedChannels.size()) std::cout << ",";
            }
            std::cout << "\n";
        }
    }
    return true;
}

bool TaskPlanner::ConfigureParamsAndBuildJobs(std::vector<FileItem>& files, std::vector<Job>& jobs) {
    jobs.clear();

    AnalysisMode selectedAnalysisMode = AnalysisMode::FFT;
    std::cout << "\n分析类型：\n";
    std::cout << "1) FFT（平均频谱）\n";
    std::cout << "2) FFT vs time（时频谱）\n";
    std::cout << "3) FFT vs rpm（转速频谱图，仅ATFX）\n";
    std::cout << "4) 1/1 倍频程\n";
    std::cout << "5) 1/3 倍频程\n";
    std::cout << "6) Level vs time（声级随时间）\n";
    std::cout << "请选择（默认1）: ";

    std::string analysisStr;
    std::getline(std::cin, analysisStr);
    analysisStr = trim_ws(analysisStr);
    if (!analysisStr.empty()) {
        try {
            int a = std::stoi(analysisStr);
            if (a == 2) selectedAnalysisMode = AnalysisMode::FFT_VS_TIME;
            else if (a == 3) selectedAnalysisMode = AnalysisMode::FFT_VS_RPM;
            else if (a == 4) selectedAnalysisMode = AnalysisMode::OCTAVE_1_1;
            else if (a == 5) selectedAnalysisMode = AnalysisMode::OCTAVE_1_3;
            else if (a == 6) selectedAnalysisMode = AnalysisMode::LEVEL_VS_TIME;
        }
        catch (...) {}
    }

    std::cout << "[DEBUG] selectedAnalysisMode = " << static_cast<int>(selectedAnalysisMode) << std::endl;

    int bandsPerOctave = 1;
    OctaveMethod octaveMethod = OctaveMethod::FFT_INTEGRATION;
    double octaveRefValue = 1.0;
    if (selectedAnalysisMode == AnalysisMode::OCTAVE_1_1 || selectedAnalysisMode == AnalysisMode::OCTAVE_1_3) {
        bandsPerOctave = (selectedAnalysisMode == AnalysisMode::OCTAVE_1_1) ? 1 : 3;

        std::cout << "\n倍频程实现方法：\n";
        std::cout << "1) FFT 积分法\n";
        std::cout << "2) IIR 滤波器组\n";
        std::cout << "请选择（默认2）: ";
        std::string mm;
        std::getline(std::cin, mm);
        mm = trim_ws(mm);
        if (mm.empty()) octaveMethod = OctaveMethod::IIR_FILTERBANK;
        else {
            try {
                int mv = std::stoi(mm);
                octaveMethod = (mv == 1) ? OctaveMethod::FFT_INTEGRATION : OctaveMethod::IIR_FILTERBANK;
            }
            catch (...) {
                octaveMethod = OctaveMethod::IIR_FILTERBANK;
            }
        }

        std::cout << "倍频程参考值（默认1.0，若 dB SPL 用 20e-6）: ";
        std::string refStr;
        std::getline(std::cin, refStr);
        refStr = trim_ws(refStr);
        if (!refStr.empty()) {
            try { octaveRefValue = std::stod(refStr); }
            catch (...) { octaveRefValue = 1.0; }
        }
    }

    std::map<size_t, std::string> fileRpmChannelName;
    double rpmBinStep = 50.0;
    if (selectedAnalysisMode == AnalysisMode::FFT_VS_RPM) {
        std::cout << "\n[FFT vs rpm] 请输入 rpm 分箱步长（默认 50）: ";
        std::string stepStr;
        std::getline(std::cin, stepStr);
        stepStr = trim_ws(stepStr);
        if (!stepStr.empty()) {
            try {
                double v = std::stod(stepStr);
                if (v > 0.0) rpmBinStep = v;
            }
            catch (...) {}
        }

        for (size_t fi = 0; fi < files.size(); ++fi) {
            if (!files[fi].selected || files[fi].ext != "atfx") continue;
            if (files[fi].channels.empty()) continue;

            std::cout << "\n[FFT vs rpm] 文件[" << (fi + 1) << "] " << files[fi].path << "\n";
            std::cout << "请选择转速通道ID（单选）: ";
            std::string rpmSel;
            std::getline(std::cin, rpmSel);
            auto rpmIdx = parse_indices_csv_1based_safe(rpmSel, files[fi].channels.size());

            if (rpmIdx.empty()) {
                std::cerr << "[错误] 未选择 rpm 通道，文件跳过: " << files[fi].path << "\n";
                files[fi].selected = false;
                continue;
            }

            size_t rpmCi = rpmIdx.front();
            if (rpmCi >= files[fi].channels.size()) {
                std::cerr << "[错误] rpm 通道索引越界，文件跳过: " << files[fi].path << "\n";
                files[fi].selected = false;
                continue;
            }

            fileRpmChannelName[fi] = files[fi].channels[rpmCi].channelName;
        }
    }

    std::map<size_t, FFTParams> fileParams;
    std::map<unsigned long long, FFTParams> channelParams;

    std::cout << "\n参数模式：\n";
    std::cout << "1) 统一参数\n";
    std::cout << "2) 按文件参数\n";
    std::cout << "3) 按通道参数\n";
    std::cout << "请选择（默认1）: ";
    std::string modeStr;
    std::getline(std::cin, modeStr);

    int mode = 1;
    if (!trim_ws(modeStr).empty()) {
        try {
            int t = std::stoi(modeStr);
            if (t == 2 || t == 3) mode = t;
        }
        catch (...) {}
    }

    std::cout << "[DEBUG] parameter mode = " << mode << std::endl;

    if (mode == 1) {
        FFTParams shared{};
        if (selectedAnalysisMode == AnalysisMode::LEVEL_VS_TIME) {
            AskLevelVsTimeParams(shared, "[统一参数设置]");
        }
        else {
            shared = AskFFTParamsWithTitle("[统一参数设置]");
            ApplyModeSpecificParams(shared, selectedAnalysisMode, bandsPerOctave, octaveMethod, octaveRefValue, "[统一参数设置]");
        }

        std::cout << std::fixed << std::setprecision(9)
            << "[DEBUG] shared params after input:"
            << " time_weighting=" << shared.time_weighting
            << " time_constant=" << shared.level_time_constant_sec
            << " window_sec=" << shared.level_window_sec
            << " output_step=" << shared.level_output_step_sec
            << " calibration=" << shared.calibrationFactor
            << " ref=" << shared.octaveRefValue
            << std::endl;

        for (size_t fi = 0; fi < files.size(); ++fi) {
            if (files[fi].selected) fileParams[fi] = shared;
        }
    }
    else if (mode == 2) {
        for (size_t fi = 0; fi < files.size(); ++fi) {
            if (!files[fi].selected) continue;
            std::string title = "[按文件参数] 文件[" + std::to_string(fi + 1) + "] " + files[fi].path;

            FFTParams p{};
            if (selectedAnalysisMode == AnalysisMode::LEVEL_VS_TIME) {
                AskLevelVsTimeParams(p, title);
            }
            else {
                p = AskFFTParamsWithTitle(title);
                ApplyModeSpecificParams(p, selectedAnalysisMode, bandsPerOctave, octaveMethod, octaveRefValue, title);
            }

            std::cout << std::fixed << std::setprecision(9)
                << "[DEBUG] fileParams[" << fi << "]:"
                << " time_weighting=" << p.time_weighting
                << " time_constant=" << p.level_time_constant_sec
                << " window_sec=" << p.level_window_sec
                << " output_step=" << p.level_output_step_sec
                << " calibration=" << p.calibrationFactor
                << " ref=" << p.octaveRefValue
                << std::endl;

            fileParams[fi] = p;
        }
    }
    else {
        FFTParams lastParams{};
        bool hasLast = false;

        for (size_t fi = 0; fi < files.size(); ++fi) {
            if (!files[fi].selected) continue;

            if (files[fi].ext == "wav") {
                std::string title = "[按通道参数-WAV] 文件[" + std::to_string(fi + 1) + "] " + files[fi].path;
                if (hasLast && ask_reuse_default_yes("WAV 是否复用上一次参数？")) {
                    fileParams[fi] = lastParams;
                }
                else {
                    FFTParams p{};
                    if (selectedAnalysisMode == AnalysisMode::LEVEL_VS_TIME) {
                        p = hasLast ? lastParams : FFTParams{};
                        AskLevelVsTimeParams(p, title);
                    }
                    else {
                        p = AskFFTParamsWithTitle(title, hasLast ? lastParams : FFTParams{});
                        ApplyModeSpecificParams(p, selectedAnalysisMode, bandsPerOctave, octaveMethod, octaveRefValue, title);
                    }
                    fileParams[fi] = p;
                    lastParams = p;
                    hasLast = true;
                }
            }
            else if (files[fi].ext == "atfx" || files[fi].ext == "hdf") {
                for (size_t ci : files[fi].selectedChannels) {
                    if (ci >= files[fi].channels.size()) continue;
                    const auto& ch = files[fi].channels[ci];
                    std::string title =
                        "[按通道参数] 文件[" + std::to_string(fi + 1) + "] " + files[fi].path +
                        "\n            通道[" + std::to_string(ci + 1) + "] " + ch.channelName +
                        (ch.unit.empty() ? "" : (" [" + ch.unit + "]"));

                    unsigned long long key =
                        (static_cast<unsigned long long>(fi) << 32ULL) |
                        static_cast<unsigned long long>(ci);

                    if (hasLast && ask_reuse_default_yes("通道 \"" + ch.channelName + "\" 是否复用上一次参数？")) {
                        channelParams[key] = lastParams;
                    }
                    else {
                        FFTParams p{};
                        if (selectedAnalysisMode == AnalysisMode::LEVEL_VS_TIME) {
                            p = hasLast ? lastParams : FFTParams{};
                            AskLevelVsTimeParams(p, title);
                        }
                        else {
                            p = AskFFTParamsWithTitle(title, hasLast ? lastParams : FFTParams{});
                            ApplyModeSpecificParams(p, selectedAnalysisMode, bandsPerOctave, octaveMethod, octaveRefValue, title);
                        }
                        channelParams[key] = p;
                        lastParams = p;
                        hasLast = true;
                    }

                    const FFTParams& dbg = channelParams[key];
                    std::cout << std::fixed << std::setprecision(9)
                        << "[DEBUG] channelParams[file=" << fi << ", ch=" << ci << "]:"
                        << " time_weighting=" << dbg.time_weighting
                        << " time_constant=" << dbg.level_time_constant_sec
                        << " window_sec=" << dbg.level_window_sec
                        << " output_step=" << dbg.level_output_step_sec
                        << " calibration=" << dbg.calibrationFactor
                        << " ref=" << dbg.octaveRefValue
                        << std::endl;
                }
            }
        }
    }

    for (size_t fi = 0; fi < files.size(); ++fi) {
        if (!files[fi].selected) continue;

        if (files[fi].ext == "atfx") {
            for (size_t ci : files[fi].selectedChannels) {
                if (ci >= files[fi].channels.size()) continue;

                Job j;
                j.fileIdx = fi;
                j.isATFX = true;
                j.channelIdx = ci;
                j.mode = selectedAnalysisMode;

                if (selectedAnalysisMode == AnalysisMode::FFT_VS_RPM) {
                    auto itRpm = fileRpmChannelName.find(fi);
                    if (itRpm == fileRpmChannelName.end() || itRpm->second.empty()) continue;
                    j.rpmChannelName = itRpm->second;
                    j.rpmBinStep = rpmBinStep;
                }

                if (mode == 3) {
                    unsigned long long key =
                        (static_cast<unsigned long long>(fi) << 32ULL) |
                        static_cast<unsigned long long>(ci);
                    auto it = channelParams.find(key);
                    if (it == channelParams.end()) continue;
                    j.params = it->second;
                }
                else {
                    auto it = fileParams.find(fi);
                    if (it == fileParams.end()) continue;
                    j.params = it->second;
                }

                std::cout << std::fixed << std::setprecision(9)
                    << "[DEBUG] Build Job ATFX file=" << fi
                    << " ch=" << ci
                    << " mode=" << static_cast<int>(j.mode)
                    << " params.time_weighting=" << j.params.time_weighting
                    << " params.time_constant=" << j.params.level_time_constant_sec
                    << " params.window_sec=" << j.params.level_window_sec
                    << " params.output_step=" << j.params.level_output_step_sec
                    << " params.calibration=" << j.params.calibrationFactor
                    << " params.ref=" << j.params.octaveRefValue
                    << std::endl;

                jobs.push_back(j);
            }
        }
        else if (files[fi].ext == "hdf") {
            if (selectedAnalysisMode == AnalysisMode::FFT_VS_RPM) continue;

            for (size_t ci : files[fi].selectedChannels) {
                if (ci >= files[fi].channels.size()) continue;

                Job j;
                j.fileIdx = fi;
                j.isATFX = false;
                j.channelIdx = ci;
                j.mode = selectedAnalysisMode;

                if (mode == 3) {
                    unsigned long long key =
                        (static_cast<unsigned long long>(fi) << 32ULL) |
                        static_cast<unsigned long long>(ci);
                    auto it = channelParams.find(key);
                    if (it == channelParams.end()) continue;
                    j.params = it->second;
                }
                else {
                    auto it = fileParams.find(fi);
                    if (it == fileParams.end()) continue;
                    j.params = it->second;
                }

                std::cout << std::fixed << std::setprecision(9)
                    << "[DEBUG] Build Job HDF file=" << fi
                    << " ch=" << ci
                    << " mode=" << static_cast<int>(j.mode)
                    << " params.time_weighting=" << j.params.time_weighting
                    << " params.time_constant=" << j.params.level_time_constant_sec
                    << " params.window_sec=" << j.params.level_window_sec
                    << " params.output_step=" << j.params.level_output_step_sec
                    << " params.calibration=" << j.params.calibrationFactor
                    << " params.ref=" << j.params.octaveRefValue
                    << std::endl;

                jobs.push_back(j);
            }
        }
        else if (files[fi].ext == "wav") {
            if (selectedAnalysisMode == AnalysisMode::FFT_VS_RPM) continue;

            auto it = fileParams.find(fi);
            if (it == fileParams.end()) continue;

            Job j;
            j.fileIdx = fi;
            j.isATFX = false;
            j.channelIdx = 0;
            j.mode = selectedAnalysisMode;
            j.params = it->second;

            std::cout << std::fixed << std::setprecision(9)
                << "[DEBUG] Build Job WAV file=" << fi
                << " mode=" << static_cast<int>(j.mode)
                << " params.time_weighting=" << j.params.time_weighting
                << " params.time_constant=" << j.params.level_time_constant_sec
                << " params.window_sec=" << j.params.level_window_sec
                << " params.output_step=" << j.params.level_output_step_sec
                << " params.calibration=" << j.params.calibrationFactor
                << " params.ref=" << j.params.octaveRefValue
                << std::endl;

            jobs.push_back(j);
        }
    }

    std::cout << "[DBG] Build jobs done: " << jobs.size() << "\n";
    return !jobs.empty();
}