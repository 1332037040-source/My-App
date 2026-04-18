#include "Analyzer.h"
#include "preprocess/AudioBlocking.h"
#include "preprocess/Preprocessing.h"
#include "preprocess/Window.h"
#include "fft/FFTCore.h"
#include "core/Utils.h"

#include <iostream>
#include <fstream>
#include <cmath>
#include <iomanip>
#include <algorithm>
#include <complex>
#include <vector>

namespace Analyzer {

    // --- 既有 compute_averaged_fft 保持不改（若需要可合并） ---
    CVector compute_averaged_fft(const DVector& audio, size_t block_size, double overlap,
        Window::WindowType wtype, AmplitudeScaling scaling)
    {
        // 保留你之前的实现（若已有更精确实现请替换）
        auto blocks = AudioBlocking::split_audio_into_blocks(audio, block_size, overlap);
        if (blocks.empty()) {
            std::cerr << "错误：无有效数据块！" << std::endl;
            return {};
        }

        size_t fft_len = Utils::next_power_of_2(block_size);
        DVector avg_power(fft_len, 0.0);

        auto w = Window::generate_window(wtype, block_size);
        double window_sum = 0.0;
        for (double v : w) window_sum += v;
        double window_mean = (block_size > 0) ? (window_sum / static_cast<double>(block_size)) : 1.0;
        double window_correction = (window_mean > 0.0) ? (1.0 / window_mean) : 1.0;

        size_t frame_count = 0;
        for (const auto& b : blocks) {
            if (b.size() < block_size) continue;
            DVector x = b;
            Preprocessing::remove_dc(x);

            for (size_t i = 0; i < block_size; ++i) x[i] *= w[i];

            CVector in(fft_len, 0.0);
            for (size_t i = 0; i < block_size; ++i) in[i] = x[i];

            auto fft_result = FFTCore::fft(in);

            for (size_t i = 0; i < fft_len; ++i) {
                double re = fft_result[i].real();
                double im = fft_result[i].imag();
                avg_power[i] += (re * re + im * im);
            }
            ++frame_count;
        }

        if (frame_count == 0) return {};

        for (size_t i = 0; i < fft_len; ++i) avg_power[i] /= static_cast<double>(frame_count);

        CVector out(fft_len);
        for (size_t i = 0; i < fft_len; ++i) {
            double mag = std::sqrt(avg_power[i]);
            double amp = 2.0 * mag / static_cast<double>(block_size);
            amp *= window_correction;
            if (scaling == AmplitudeScaling::RMS) amp /= std::sqrt(2.0);
            out[i] = std::complex<double>(amp, 0.0);
        }

        return out;
    }

    // --- 新增：基于 Welch 的 PSD 估计（one-sided PSD） ---
    // 返回长度 nfft/2 + 1, 单位为 signal^2 / Hz（例如 Pa^2/Hz 若输入为 Pa）
    std::vector<double> compute_averaged_psd(
        const std::vector<double>& audio,
        size_t block_size,
        double overlap,
        Window::WindowType wtype,
        unsigned int sample_rate)
    {
        using std::vector;
        vector<vector<double>> blocks = AudioBlocking::split_audio_into_blocks(audio, block_size, overlap);
        if (blocks.empty()) {
            std::cerr << "[PSD] No valid blocks\n";
            return {};
        }

        size_t nfft = Utils::next_power_of_2(block_size);
        vector<double> avg_sq(nfft, 0.0);
        size_t K = 0;

        auto w = Window::generate_window(wtype, block_size);

        // U = (1/N) * sum w[n]^2
        double U = 0.0;
        for (size_t n = 0; n < block_size; ++n) U += (w[n] * w[n]);
        U /= static_cast<double>(block_size);
        if (U <= 0.0) U = 1.0;

        for (const auto& seg : blocks) {
            if (seg.size() < block_size) continue;

            std::vector<std::complex<double>> in(nfft);
            for (size_t i = 0; i < block_size; ++i) in[i] = std::complex<double>(seg[i] * w[i], 0.0);
            for (size_t i = block_size; i < nfft; ++i) in[i] = std::complex<double>(0.0, 0.0);

            auto X = FFTCore::fft(in); // length nfft

            for (size_t k = 0; k < nfft; ++k) {
                double re = X[k].real();
                double im = X[k].imag();
                avg_sq[k] += (re * re + im * im);
            }
            ++K;
        }

        if (K == 0) {
            std::cerr << "[PSD] No usable frames\n";
            return {};
        }

        for (size_t k = 0; k < nfft; ++k) avg_sq[k] /= static_cast<double>(K);

        double fs = static_cast<double>(sample_rate);
        size_t nfft_half = nfft / 2 + 1;
        vector<double> psd_one_sided(nfft_half, 0.0);

        for (size_t k = 0; k < nfft_half; ++k) {
            // PSD two-sided estimate = avg_sq / (fs * U)
            double pxx = avg_sq[k] / (fs * U);
            // convert to one-sided (double non-DC and non-Nyquist bins)
            if (k > 0 && k < nfft / 2) pxx *= 2.0;
            psd_one_sided[k] = pxx; // units: signal^2 / Hz
        }

        return psd_one_sided;
    }

    void write_csv(const CVector& fft_res, uint32_t fs, const std::string& path)
    {
        std::ofstream f(path);
        if (!f) {
            std::cerr << "错误：无法创建报告文件 " << path << std::endl;
            return;
        }

        f << "Frequency(Hz),Magnitude,Mag_dB\n";

        size_t n = fft_res.size();
        double freq_resolution = static_cast<double>(fs) / static_cast<double>(n);

        for (size_t i = 0; i < n / 2; ++i) {
            double freq = static_cast<double>(i) * freq_resolution;
            double mag = std::abs(fft_res[i]);
            double mag_db = (mag > 1e-10) ? 20 * std::log10(mag) : -100.0;

            f << std::fixed << std::setprecision(6) << freq << ","
                << std::fixed << std::setprecision(8) << mag << ","
                << std::fixed << std::setprecision(2) << mag_db << "\n";
        }

        f.close();
        std::cout << "FFT报告已保存至: " << path << std::endl;
    }

} // namespace Analyzer
