#pragma once
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
    LEVEL_VS_TIME = 5,
    LEVEL_VS_RPM = 6
};

// 倍频程实现方法
enum class OctaveMethod {
    FFT_INTEGRATION = 0,
    IIR_FILTERBANK = 1
};

struct FFTParams {
    size_t block_size = 8192;
    double overlap_ratio = 0.5;
    Window::WindowType window_type = Window::WindowType::Rectangular;
    Analyzer::AmplitudeScaling amp_scaling = Analyzer::AmplitudeScaling::Peak;
    Weighting::WeightType weight_type = Weighting::WeightType::None;

    int bandsPerOctave = 1;
    OctaveMethod octaveMethod = OctaveMethod::FFT_INTEGRATION;
    double octaveRefValue = 20e-6;
    double calibrationFactor = 1.0;

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
    double rpmBinStep = 50.0;

    // 新增：该Job是否允许写CSV
    bool writeCsvToDisk = true;
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
    std::string fftCsvPath;
    std::string levelVsTimeCsvPath;
    std::string levelVsRpmCsvPath;
    PeakPreview peak;

    std::vector<double> curveX;
    std::vector<double> curveY;
    bool curveIsDb = false;
    std::string curveXUnit;
    std::string curveYUnit;
    std::string curveName;

    std::vector<std::vector<double>> heatmapFreqFrames;
    std::vector<std::vector<double>> heatmapAmpFrames;
    bool heatmapIsDb = false;
    std::string heatmapXUnit;
    std::string heatmapYUnit;
    std::string heatmapZUnit;
};

struct SignalData {
    std::vector<double> samples;
    double fs = 0.0;
    std::string channelName;
    std::string unit;
    std::string sourcePath;
};