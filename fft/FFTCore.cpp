#include "FFTCore.h"
#include <cmath>

namespace FFTCore {
    void fft_base2(CVector& x, bool invert) {
        size_t n = x.size();
        for (size_t i = 1, j = 0; i < n; i++) {
            size_t bit = n >> 1;
            for (; j & bit; bit >>= 1) j ^= bit;
            j ^= bit;
            if (i < j) std::swap(x[i], x[j]);
        }
        for (size_t len = 2; len <= n; len <<= 1) {
            double ang = 2 * M_PI / len * (invert ? -1 : 1);
            Complex wlen(cos(ang), sin(ang));
            for (size_t i = 0; i < n; i += len) {
                Complex w(1);
                for (size_t j = 0; j < len / 2; j++) {
                    Complex u = x[i + j];
                    Complex v = x[i + j + len / 2] * w;
                    x[i + j] = u + v;
                    x[i + j + len / 2] = u - v;
                    w *= wlen;
                }
            }
        }
        if (invert) for (auto& elem : x) elem /= (double)n;
    }

    CVector fft(CVector x) {
        fft_base2(x, false);
        return x;
    }
}
