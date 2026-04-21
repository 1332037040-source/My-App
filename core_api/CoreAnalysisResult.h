#pragma once
#include <string>
#include <vector>

struct CoreCurve2D
{
    std::vector<double> x;   // 频率轴(Hz)或时间轴(s)
    std::vector<double> y;   // 幅值
    bool isDb = false;       // y 是否为 dB
    std::string xUnit;       // "Hz" / "s"
    std::string yUnit;       // "Pa" / "dB" / ...
    std::string name;        // 曲线名（可选）
};

struct CoreHeatmap3D
{
    // 每一帧一条曲线：freqFrames[i].size == ampFrames[i].size
    std::vector<std::vector<double>> freqFrames;
    std::vector<std::vector<double>> ampFrames;

    bool isDb = false;
    std::string xUnit;       // "Hz"
    std::string yUnit;       // "Frame"/"Time"/"RPM"
    std::string zUnit;       // "Pa"/"dB"
};

struct CoreAnalysisResult
{
    bool success = false;
    std::string message;

    std::string timeSignalCsv;
    std::string fftCsv;
    std::string spectrogramCsv;
    std::string octaveCsv;
    std::string reportFile;
    std::string levelVsTimeCsv;
    std::string levelVsRpmCsv;

    double peakFrequency = 0.0;
    double peakValue = 0.0;

    // ===== 新增：内存结果接口 =====
    bool hasCurve2D = false;
    CoreCurve2D curve2D;

    bool hasHeatmap3D = false;
    CoreHeatmap3D heatmap3D;

    std::vector<std::string> generatedFiles;
};