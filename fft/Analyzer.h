#pragma once
#ifndef FFT11_ANALYZER_H
#define FFT11_ANALYZER_H

#include <vector>
#include <complex>
#include <cstdint>
#include <string>

// 使用项目已有的窗口类型定义，请确保该头路径在你的工程中存在
// （这是关键：不要在这里重新定义 Window::WindowType）
#include "preprocess/Window.h"

using DVector = std::vector<double>;
using CVector = std::vector<std::complex<double>>;

namespace Analyzer {

    enum class AmplitudeScaling {
        Peak,
        RMS
    };

    // compute_averaged_fft:
    // 返回长度 nfft（next power of two >= block_size）的复向量，
    // 使用者通常取单边部分 [0..nfft/2]
    CVector compute_averaged_fft(
        const DVector& audio,
        size_t block_size,
        double overlap,
        Window::WindowType wtype,
        AmplitudeScaling scaling
    );

    // compute_averaged_psd (Welch-style)
    // 返回 one-sided PSD 长度 = nfft/2 + 1，单位 signal^2/Hz（若输入单位为 Pa 则为 Pa^2/Hz）
    std::vector<double> compute_averaged_psd(
        const std::vector<double>& audio,
        size_t block_size,
        double overlap,
        Window::WindowType wtype,
        unsigned int sample_rate
    );

    // write CSV for FFT result (single-sided)
    void write_csv(const CVector& fft_res, uint32_t fs, const std::string& path);

} // namespace Analyzer

#endif // FFT11_ANALYZER_H
