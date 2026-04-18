#include "Window.h"
#include <cmath>

namespace Window {

    DVector generate_window(WindowType type, size_t window_len) {
        DVector window(window_len, 1.0);
        if (window_len <= 1) return window;

        const double N = static_cast<double>(window_len - 1);

        switch (type) {
        case WindowType::Rectangular:
            // 默认全1，已在初始化完成
            break;

        case WindowType::Hanning:
            for (size_t n = 0; n < window_len; ++n) {
                window[n] = 0.5 * (1.0 - std::cos(2.0 * M_PI * n / N));
            }
            break;

        case WindowType::Hamming:
            for (size_t n = 0; n < window_len; ++n) {
                window[n] = 0.54 - 0.46 * std::cos(2.0 * M_PI * n / N);
            }
            break;

        case WindowType::Blackman:
            for (size_t n = 0; n < window_len; ++n) {
                window[n] = 0.42
                    - 0.5 * std::cos(2.0 * M_PI * n / N)
                    + 0.08 * std::cos(4.0 * M_PI * n / N);
            }
            break;

        case WindowType::Bartlett:
            // 三角窗：1 - |(n - N/2)/(N/2)|
            for (size_t n = 0; n < window_len; ++n) {
                window[n] = 1.0 - std::abs((static_cast<double>(n) - N / 2.0) / (N / 2.0));
            }
            break;

        case WindowType::FlatTop:
            // 常用5项平顶窗（HFT95近似系数）
            // w[n] = a0 - a1 cos(2πn/N) + a2 cos(4πn/N) - a3 cos(6πn/N) + a4 cos(8πn/N)
        {
            const double a0 = 1.0;
            const double a1 = 1.93;
            const double a2 = 1.29;
            const double a3 = 0.388;
            const double a4 = 0.028;
            for (size_t n = 0; n < window_len; ++n) {
                double x = 2.0 * M_PI * n / N;
                window[n] = a0
                    - a1 * std::cos(x)
                    + a2 * std::cos(2.0 * x)
                    - a3 * std::cos(3.0 * x)
                    + a4 * std::cos(4.0 * x);
            }
        }
        break;

        default:
            // 未知类型回退矩形窗
            break;
        }

        return window;
    }

} // namespace Window
