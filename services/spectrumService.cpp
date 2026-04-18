#include "SpectrumService.h"

#include "../fft/Analyzer.h"
#include "../preprocess/Weighting.h"

#include <cmath>
#include <limits>

namespace {
    static inline uint32_t ToFsU32(double fs) {
        if (fs <= 0.0) return 0;
        if (fs > static_cast<double>(std::numeric_limits<uint32_t>::max())) {
            return std::numeric_limits<uint32_t>::max();
        }
        return static_cast<uint32_t>(std::llround(fs));
    }
} // namespace

bool SpectrumService::ComputeAveragedFFT(const SignalData& sig,
    const Job& job,
    CVector& outFft,
    std::string& err) const
{
    err.clear();

    outFft = Analyzer::compute_averaged_fft(sig.samples,
        job.params.block_size,
        job.params.overlap_ratio,
        job.params.window_type,
        job.params.amp_scaling);

    if (outFft.empty()) {
        err = "FFT计算失败";
        return false;
    }
    return true;
}

void SpectrumService::ApplyWeighting(CVector& fft,
    double fs,
    const Job& job) const
{
    Weighting::apply_weighting(fft, ToFsU32(fs), job.params.weight_type);
}

bool SpectrumService::ComputeWeightedAveragedFFT(const SignalData& sig,
    const Job& job,
    CVector& outFft,
    std::string& err) const
{
    if (!ComputeAveragedFFT(sig, job, outFft, err)) {
        return false;
    }
    ApplyWeighting(outFft, sig.fs, job);
    return true;
}

PeakPreview SpectrumService::CalcPeakFromFFT(const CVector& fft, double fs) const
{
    PeakPreview peak{};
    if (fft.empty() || fs <= 0.0) return peak;

    const size_t nfft = fft.size();
    const size_t nbins = nfft / 2 + 1;
    if (nbins == 0) return peak;

    size_t max_idx = 0;
    double max_mag = std::abs(fft[0]);

    for (size_t k = 1; k < nbins; ++k) {
        const double mag = std::abs(fft[k]);
        if (mag > max_mag) {
            max_mag = mag;
            max_idx = k;
        }
    }

    peak.idx = max_idx;
    peak.freq = static_cast<double>(max_idx) * fs / static_cast<double>(nfft);
    peak.mag = max_mag;
    return peak;
}

PeakPreview SpectrumService::CalcPeakFromValues(const std::vector<double>& mags,
    const std::vector<double>& freqs) const
{
    PeakPreview peak{};
    if (mags.empty() || freqs.empty()) return peak;

    const size_t n = (mags.size() < freqs.size()) ? mags.size() : freqs.size();
    if (n == 0) return peak;

    size_t max_idx = 0;
    double max_mag = mags[0];

    for (size_t i = 1; i < n; ++i) {
        if (mags[i] > max_mag) {
            max_mag = mags[i];
            max_idx = i;
        }
    }

    peak.idx = max_idx;
    peak.freq = freqs[max_idx];
    peak.mag = max_mag;
    return peak;
}