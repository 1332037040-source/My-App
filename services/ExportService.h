#pragma once

#include "../domain/Types.h"
#include "../domain/Spectrogram.h"
#include "../domain/RpmSpectrogram.h"
#include "../features/octave_band/OctaveAnalyzer.h"
#include "../features/level_vs_time/LevelVsTimeAnalyzer.h"
#include <string>
#include <vector>

class ExportService {
public:
    // 导出普通 FFT（复用 Analyzer::write_csv）
    bool WriteFFT(const CVector& fft,
        double fs,
        const std::string& outPath,
        std::string& err) const;

    // 导出时频谱（格式与原 FFTExecutor 保持一致）
    bool WriteSpectrogram(const Spectrogram& sp,
        const std::string& outPath,
        std::string& err) const;

    // 导出转速频谱
    bool WriteRpmSpectrogram(const RpmSpectrogram& sp,
        const std::string& outPath,
        std::string& err) const;

    // 导出时域信号
    bool WriteTimeSignal(const std::vector<double>& x,
        const std::string& outPath,
        std::string& err) const;

    // 导出 Octave 结果
    bool WriteOctave(const OctaveBandResult& result,
        const std::string& outPath,
        std::string& err) const;

    // 导出 Level vs Time 结果
    bool WriteLevelVsTime(const LevelVsTimeAnalyzer::LevelSeries& series,
        const std::string& outPath,
        std::string& err) const;
};