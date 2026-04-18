#include "features/octave_band/OctaveAnalyzer.h"
#include <cmath>
#include <fstream>
#include <iomanip>
#include <algorithm>

// 1/1 octave centers (保留你最初使用的完整中心频点范围)
static const std::vector<double> octave1_center = {
    6.3, 8, 10, 12.5, 16, 20, 25, 31.5, 40, 50, 63, 80,
    100, 125, 160, 200, 250, 315, 400, 500, 630, 800,
    1000, 1250, 1600, 2000, 2500, 3150, 4000, 5000, 6300,
    8000, 10000, 12500, 16000, 20000, 25000
};

// 1/3 octave centers（此处先沿用同一组中心频点；如需严格标准表可再单独替换）
static const std::vector<double> octave3_center = {
    6.3, 8, 10, 12.5, 16, 20, 25, 31.5, 40, 50, 63, 80,
    100, 125, 160, 200, 250, 315, 400, 500, 630, 800,
    1000, 1250, 1600, 2000, 2500, 3150, 4000, 5000, 6300,
    8000, 10000, 12500, 16000, 20000, 25000
};

const std::vector<double>& get_octave_centers(OctaveBandType bandType) {
    return (bandType == OctaveBandType::Full) ? octave1_center : octave3_center;
}

// ------------------------------------------------------------------
// FFT-only octave band computation (NO PSD coefficient / NO U/G/fs scaling)
// Input:
//   avgMag: single-sided amplitudes A_k (length = nfft/2 + 1)
//   fs: sampling rate
//   nfft: FFT length used to produce avgMag
//   block_size, wtype: 为保持接口兼容，当前实现不使用
//   bandType: Full or Third
//   ref: dB参考值
//   calibrationFactor: 对幅值做线性标定
//
// Method:
//   对每个频带 [fl, fh]，按 bin 与频带重叠比例做 A_k^2 累加：
//      E_band = Σ (A_k^2 * overlap_ratio)
//   然后 band_rms = sqrt(E_band), dB = 20log10(band_rms/ref)
// ------------------------------------------------------------------
OctaveBandResult compute_octave_bands_from_fft(
    const std::vector<double>& avgMag,
    double fs,
    size_t nfft,
    size_t /*block_size*/,
    Window::WindowType /*wtype*/,
    OctaveBandType bandType,
    double ref,
    double calibrationFactor)
{
    OctaveBandResult res;
    const auto& centers = get_octave_centers(bandType);

    if (avgMag.empty() || fs <= 0.0 || nfft == 0) {
        return res;
    }

    const double df = fs / static_cast<double>(nfft);
    if (df <= 0.0) return res;

    const double tiny = 1e-20;
    const double k = (bandType == OctaveBandType::Full) ? 1.0 : (1.0 / 3.0);

    const size_t nbins = avgMag.size();

    res.bandCenters.reserve(centers.size());
    res.bandLows.reserve(centers.size());
    res.bandHighs.reserve(centers.size());
    res.bandValues.reserve(centers.size());
    res.bandValues_dB.reserve(centers.size());

    for (double fc : centers) {
        const double fl = fc / std::pow(2.0, k / 2.0);
        const double fh = fc * std::pow(2.0, k / 2.0);

        res.bandCenters.push_back(fc);
        res.bandLows.push_back(fl);
        res.bandHighs.push_back(fh);

        int leftBin = static_cast<int>(std::floor(fl / df)) - 1;
        int rightBin = static_cast<int>(std::ceil(fh / df)) + 1;

        if (leftBin < 0) leftBin = 0;
        if (rightBin >= static_cast<int>(nbins)) rightBin = static_cast<int>(nbins) - 1;
        if (rightBin < leftBin) rightBin = leftBin;

        double e_band = 0.0;

        for (int b = leftBin; b <= rightBin; ++b) {
            const double fk = static_cast<double>(b) * df;
            double bin_low = fk - 0.5 * df;
            double bin_high = fk + 0.5 * df;
            if (bin_low < 0.0) bin_low = 0.0;

            const double overlap_low = std::max(bin_low, fl);
            const double overlap_high = std::min(bin_high, fh);
            const double overlap = overlap_high - overlap_low;
            if (overlap <= 0.0) continue;

            const double bin_width = bin_high - bin_low;
            if (bin_width <= 0.0) continue;

            const double overlap_ratio = overlap / bin_width; // [0..1]
            const double A = avgMag[static_cast<size_t>(b)] * calibrationFactor;

            e_band += (A * A) * overlap_ratio;
        }

        const double band_rms = (e_band > 0.0) ? std::sqrt(e_band) : 0.0;
        res.bandValues.push_back(band_rms);

        double db = -200.0;
        if (band_rms > tiny && ref > 0.0) {
            db = 20.0 * std::log10(band_rms / ref);
        }
        res.bandValues_dB.push_back(db);
    }

    return res;
}

// ------------------------------------------------------------------
// CSV writer
// ------------------------------------------------------------------
void write_octave_csv(const OctaveBandResult& result, const std::string& path) {
    std::ofstream f(path);
    if (!f) return;

    f << "Center(Hz),Low(Hz),High(Hz),BandValue_RMS,BandValue_dB\n";
    f << std::fixed << std::setprecision(6);
    for (size_t i = 0; i < result.bandCenters.size(); ++i) {
        const double c = result.bandCenters[i];
        const double lo = result.bandLows[i];
        const double hi = result.bandHighs[i];
        const double lin = (i < result.bandValues.size()) ? result.bandValues[i] : 0.0;
        const double db = (i < result.bandValues_dB.size()) ? result.bandValues_dB[i] : -200.0;

        f << c << "," << lo << "," << hi << "," << lin << "," << std::setprecision(2) << db << "\n";
        f << std::setprecision(6);
    }
    f.close();
}
