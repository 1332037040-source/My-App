#include "ExportService.h"
#include "../fft/Analyzer.h"

#include <fstream>
#include <iomanip>
#include <limits>
#include <cmath>

static inline uint32_t ToFsU32_Export(double fs) {
    if (fs <= 0.0) return 0;
    if (fs > static_cast<double>(std::numeric_limits<uint32_t>::max())) {
        return std::numeric_limits<uint32_t>::max();
    }
    return static_cast<uint32_t>(std::llround(fs));
}

bool ExportService::WriteFFT(const CVector& fft,
    double fs,
    const std::string& outPath,
    std::string& err) const
{
    err.clear();
    Analyzer::write_csv(fft, ToFsU32_Export(fs), outPath);
    return true;
}

bool ExportService::WriteSpectrogram(const Spectrogram& sp,
    const std::string& outPath,
    std::string& err) const
{
    err.clear();

    const SpectrogramValueType valueType = SpectrogramValueType::dB;
    std::ofstream ofs(outPath);
    if (!ofs.is_open()) {
        err = "时频CSV写入失败";
        return false;
    }

    ofs << std::setprecision(10);
    ofs << "# fs=" << sp.fs
        << ", blockSize=" << sp.blockSize
        << ", hopSize=" << sp.hopSize
        << ", timeBins=" << sp.timeBins
        << ", freqBins=" << sp.freqBins
        << ", valueType=" << (valueType == SpectrogramValueType::dB ? "dB" : "linear")
        << "\n";

    ofs << "freq_hz";
    for (size_t t = 0; t < sp.timeBins; ++t) {
        double ts = (t < sp.frameTimeSec.size()) ? sp.frameTimeSec[t] : 0.0;
        ofs << "," << ts;
    }
    ofs << "\n";

    const double df = (sp.blockSize > 0) ? (sp.fs / static_cast<double>(sp.blockSize)) : 0.0;
    for (size_t k = 0; k < sp.freqBins; ++k) {
        const double f = static_cast<double>(k) * df;
        ofs << f;
        for (size_t t = 0; t < sp.timeBins; ++t) {
            double v = (valueType == SpectrogramValueType::dB) ? sp.atDb(t, k) : sp.atLinear(t, k);
            ofs << "," << v;
        }
        ofs << "\n";
    }

    if (!ofs.good()) {
        err = "时频CSV写入失败";
        return false;
    }
    return true;
}

bool ExportService::WriteRpmSpectrogram(const RpmSpectrogram& sp,
    const std::string& outPath,
    std::string& err) const
{
    err.clear();

    std::ofstream ofs(outPath);
    if (!ofs.is_open()) {
        err = "转速频谱CSV写入失败";
        return false;
    }

    ofs << std::setprecision(10);

    // 元信息
    ofs << "# fs=" << sp.fs
        << ", blockSize=" << sp.blockSize
        << ", rpmMin=" << sp.rpmMin
        << ", rpmMax=" << sp.rpmMax
        << ", rpmStep=" << sp.rpmStep
        << ", rpmBins=" << sp.rpmBins
        << ", freqBins=" << sp.freqBins
        << "\n";

    // 表头：第一列频率，后续列为rpm（横轴）
    ofs << "freq_hz";
    for (size_t r = 0; r < sp.rpmBins; ++r) {
        const double rpm = sp.rpmMin + static_cast<double>(r) * sp.rpmStep;
        ofs << "," << rpm;
    }
    ofs << "\n";

    // 数据：每行一个频率点（纵轴）
    // 为对齐Artemis观感，从 k=1 开始（跳过0Hz���
    const double df = (sp.blockSize > 0)
        ? (sp.fs / static_cast<double>(sp.blockSize))
        : 0.0;

    for (size_t k = 1; k < sp.freqBins; ++k) {
        const double freq = static_cast<double>(k) * df;
        ofs << freq;

        for (size_t r = 0; r < sp.rpmBins; ++r) {
            ofs << "," << sp.at(r, k); // dataDb[r * freqBins + k]
        }
        ofs << "\n";
    }

    if (!ofs.good()) {
        err = "转速频谱CSV写入失败";
        return false;
    }
    return true;
}

bool ExportService::WriteTimeSignal(const std::vector<double>& x,
    const std::string& outPath,
    std::string& err) const
{
    err.clear();

    std::ofstream ofs(outPath);
    if (!ofs.is_open()) {
        err = "时间CSV写入失败";
        return false;
    }

    ofs << "Index,Value\n";
    for (size_t i = 0; i < x.size(); ++i) {
        ofs << i << "," << x[i] << "\n";
    }

    if (!ofs.good()) {
        err = "时间CSV写入失败";
        return false;
    }

    return true;
}

bool ExportService::WriteOctave(const OctaveBandResult& result,
    const std::string& outPath,
    std::string& err) const
{
    err.clear();

    std::ofstream ofs(outPath);
    if (!ofs.is_open()) {
        err = "倍频程CSV写入失败";
        return false;
    }

    ofs << "CenterHz,Value\n";

    const size_t n = (result.bandCenters.size() < result.bandValues.size())
        ? result.bandCenters.size()
        : result.bandValues.size();

    for (size_t i = 0; i < n; ++i) {
        ofs << result.bandCenters[i] << "," << result.bandValues[i] << "\n";
    }

    if (!ofs.good()) {
        err = "倍频程CSV写入失败";
        return false;
    }

    return true;
}

bool ExportService::WriteLevelVsTime(const LevelVsTimeAnalyzer::LevelSeries& series,
    const std::string& outPath,
    std::string& err) const
{
    err.clear();

    std::ofstream ofs(outPath);
    if (!ofs.is_open()) {
        err = "Level vs Time CSV写入失败";
        return false;
    }

    ofs << "time_sec,level_db\n";
    for (const auto& pt : series.points) {
        ofs << pt.timeSec << "," << pt.levelDb << "\n";
    }

    if (!ofs.good()) {
        err = "Level vs Time CSV写入失败";
        return false;
    }

    return true;
}
bool ExportService::WriteLevelVsRpm(const LevelVsRpmSeries& series,
    const std::string& outPath,
    std::string& err) const
{
    std::ofstream ofs(outPath);
    if (!ofs.is_open()) {
        err = "无法打开输出文件: " + outPath;
        return false;
    }

    ofs << "rpm,level_db\n";
    ofs << std::fixed << std::setprecision(6);

    for (const auto& p : series.points) {
        ofs << p.rpm << "," << p.levelDb << "\n";
    }

    if (!ofs.good()) {
        err = "写入Level vs RPM失败: " + outPath;
        return false;
    }

    return true;
}