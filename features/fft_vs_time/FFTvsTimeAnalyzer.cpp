#include "FFTvsTimeAnalyzer.h"
#include "fft/Analyzer.h"
#include "preprocess/Preprocessing.h"
#include "preprocess/Weighting.h"
#include <algorithm>
#include <cmath>

namespace FFTvsTimeAnalyzer {

    Spectrogram Compute(const DVector& x, double fs, const FFTParams& p)
    {
        Spectrogram sp;
        sp.fs = fs;
        sp.blockSize = p.block_size;

        if (x.empty() || fs <= 0.0 || p.block_size < 2) return sp;

        size_t hop = static_cast<size_t>(std::llround(p.block_size * (1.0 - p.overlap_ratio)));
        if (hop == 0) hop = 1;
        sp.hopSize = hop;

        if (x.size() < p.block_size) return sp;

        sp.timeBins = 1 + (x.size() - p.block_size) / hop;
        sp.freqBins = p.block_size / 2 + 1;

        sp.dataLinear.assign(sp.timeBins * sp.freqBins, 0.0);
        sp.dataDb.assign(sp.timeBins * sp.freqBins, -300.0);
        sp.frameTimeSec.assign(sp.timeBins, 0.0);

        constexpr double EPS = 1e-20;

        // dB参考值：默认20uPa（SPL）
        const double ref = (p.octaveRefValue > 0.0) ? p.octaveRefValue : 20e-6;
        // 线性标定：把当前单位换算到Pa（若已是Pa可设为1）
        const double cal = p.calibrationFactor;

        for (size_t t = 0; t < sp.timeBins; ++t) {
            const size_t start = t * hop;

            DVector frame(p.block_size);
            std::copy(x.begin() + start, x.begin() + start + p.block_size, frame.begin());

            // 帧中心时刻
            sp.frameTimeSec[t] =
                (static_cast<double>(start) + 0.5 * static_cast<double>(p.block_size)) / fs;

            // 与平均FFT保持一致：先去直流
            Preprocessing::remove_dc(frame);

            // 单帧FFT（不在此函数内部做时间平均）
            CVector X = Analyzer::compute_averaged_fft(
                frame,
                p.block_size,
                0.0, // 单帧，不做帧内重叠平均
                p.window_type,
                p.amp_scaling
            );

            if (X.empty()) continue;

            // 计权（与主流程一致，频域作用）
            Weighting::apply_weighting(X, fs, p.weight_type);

            const size_t usableBins = std::min(sp.freqBins, X.size());
            for (size_t k = 0; k < usableBins; ++k) {
                // 线性物理量（期望是Pa）
                const double p_lin = std::abs(X[k]) * cal;

                sp.atLinear(t, k) = p_lin;
                // dB(SPL)定义
                sp.atDb(t, k) = 20.0 * std::log10((p_lin + EPS) / ref);
            }
        }

        return sp;
    }

} // namespace FFTvsTimeAnalyzer
