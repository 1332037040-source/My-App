#pragma once
#include <limits>
#include <string>
#include <vector>
#include "preprocess/Window.h"
#include "fft/Analyzer.h"
#include "preprocess/Weighting.h"
#include "io/ATFXReader.h"

// 分析模式
enum class AnalysisMode {
    FFT = 0,
    FFT_VS_TIME = 1,
    FFT_VS_RPM = 2,
    OCTAVE_1_1 = 3,
    OCTAVE_1_3 = 4,
    LEVEL_VS_TIME = 5
};

// 倍频程实现方法
enum class OctaveMethod {
    FFT_INTEGRATION = 0,   // 频谱积分法
    IIR_FILTERBANK = 1     // IIR 滤波器组（如需实现）
};

struct FFTParams {
    size_t block_size = 8192;
    double overlap_ratio = 0.5;

    // 强类型参数：与现有 ParseUtils / CoreAnalysisFacade / Engine 保持一致
    Window::WindowType window_type = Window::WindowType::Rectangular;
    Analyzer::AmplitudeScaling amp_scaling = Analyzer::AmplitudeScaling::Peak;
    Weighting::WeightType weight_type = Weighting::WeightType::None;

    // 倍频程相关
    int bandsPerOctave = 1;                 // 1 = 1/1, 3 = 1/3
    OctaveMethod octaveMethod = OctaveMethod::FFT_INTEGRATION;
    double octaveRefValue = 20e-6;          // 参考值（默认 20e-6 Pa => dB SPL）
    double calibrationFactor = 1.0;         // 全局校准因子（把当前谱单位转换为 Pa）

    // ===== Level vs Time =====
    // fast / slow / rectangle / manual / impulse
    std::string time_weighting = "fast";
    double level_time_constant_sec = 0.125;
    double level_window_sec = 0.125;
    double level_output_step_sec = 0.1;
};

struct FileItem {
    std::string path;
    std::string ext;
    bool selected = true;
    std::vector<ATFXChannelInfo> channels;
    std::vector<size_t> selectedChannels;
    double fs = 0.0;
};

struct Job {
    size_t fileIdx = 0;
    bool isATFX = false;
    size_t channelIdx = 0;
    FFTParams params;

    AnalysisMode mode = AnalysisMode::FFT;

    std::string rpmChannelName;
    size_t rpmChannelIdx = std::numeric_limits<size_t>::max();
    double rpmBinStep = 50.0;
};

struct PeakPreview {
    size_t idx = 0;
    double freq = 0.0;
    double mag = 0.0;
    double errAbs = 0.0;
    double errPct = 0.0;
};

struct JobResult {
    bool ok = false;
    std::string message;
    std::string timeCsvPath;
    std::string fftCsvPath;            // 若为倍频程，这里也写入 octave CSV 路径以便兼容
    std::string levelVsTimeCsvPath;    // Level vs Time CSV
    PeakPreview peak;
};

// 统一的时域数据结构，供 DataReaderService / SpectrumService / Flow 等共享
struct SignalData {
    std::vector<double> samples;
    double fs = 0.0;
    std::string channelName;
    std::string unit;
    std::string sourcePath; // 原始文件路径（用于生成输出名）
};
