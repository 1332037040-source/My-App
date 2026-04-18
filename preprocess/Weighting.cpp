#include "Weighting.h"
#include "fft/FFTCore.h"

#include <vector>
#include <cmath>
#include <iostream>
#include <algorithm>

namespace Weighting {

    static const std::vector<double> freq_points = {
        10, 12.5, 16, 20, 25, 31.5, 40, 50, 63, 80, 100, 125, 160, 200,
        250, 315, 400, 500, 630, 800, 1000, 1250, 1600, 2000, 2500, 3150,
        4000, 5000, 6300, 8000, 10000, 12500, 16000, 20000
    };

    static const std::vector<double> A_weight_db = {
        -70.4, -63.4, -56.7, -50.5, -44.7, -39.4, -34.6, -30.2, -26.2, -22.5,
        -19.1, -16.1, -13.4, -10.9,  -8.6,  -6.6,  -4.8,  -3.2,  -1.9,  -0.8,
          0.0,   0.6,   1.0,   1.2,   1.3,   1.2,   1.0,   0.5,  -0.1,  -1.1,
         -2.5,  -4.3,  -6.6,  -9.3
    };

    static const std::vector<double> B_weight_db = {
        -38.2, -33.2, -28.5, -24.2, -20.4, -17.1, -14.2, -11.6,  -9.3,  -7.4,
         -5.6,  -4.2,  -3.0,  -2.0,  -1.3,  -0.8,  -0.4,  -0.2,   0.0,   0.0,
          0.0,   0.0,   0.0,   0.0,   0.0,   0.0,   0.0,   0.0,   0.0,   0.0,
          0.0,   0.0,   0.0,   0.0
    };

    static const std::vector<double> C_weight_db = {
        -14.3, -11.2,  -8.5,  -6.2,  -4.4,  -3.0,  -2.0,  -1.3,  -0.8,  -0.5,
         -0.3,  -0.2,  -0.1,   0.0,   0.0,   0.0,   0.0,   0.0,   0.0,   0.0,
          0.0,   0.0,   0.0,   0.0,   0.0,   0.0,   0.0,   0.0,   0.0,   0.0,
          0.0,   0.0,   0.0,   0.0
    };

    static const std::vector<double> D_weight_db = {
        -28.0, -25.0, -22.0, -19.5, -17.0, -15.0, -13.0, -11.5, -10.0,  -8.5,
         -7.0,  -6.0,  -5.0,  -4.2,  -3.5,  -3.0,  -2.5,  -2.0,  -1.5,  -1.0,
          0.0,   0.5,   1.0,   2.0,   3.0,   4.5,   6.0,   8.0,  10.0,  11.5,
         12.0,  11.0,   9.0,   6.5
    };

    static const std::vector<double>* get_weight_table(WeightType wtype)
    {
        switch (wtype) {
        case WeightType::A: return &A_weight_db;
        case WeightType::B: return &B_weight_db;
        case WeightType::C: return &C_weight_db;
        case WeightType::D: return &D_weight_db;
        default: return nullptr;
        }
    }

    static double get_weight_db(double freq, const std::vector<double>& weight_db)
    {
        if (freq <= freq_points.front()) return weight_db.front();
        if (freq >= freq_points.back())  return weight_db.back();

        auto it = std::lower_bound(freq_points.begin(), freq_points.end(), freq);
        size_t idx = static_cast<size_t>(it - freq_points.begin());

        if (idx == 0) return weight_db[0];

        double f1 = freq_points[idx - 1];
        double f2 = freq_points[idx];
        double w1 = weight_db[idx - 1];
        double w2 = weight_db[idx];

        return w1 + (w2 - w1) * (freq - f1) / (f2 - f1);
    }

    void apply_weighting(CVector& fft_result, uint32_t sample_rate, WeightType wtype)
    {
        if (wtype == WeightType::None || fft_result.empty()) return;

        const std::vector<double>* weight_db = get_weight_table(wtype);
        if (!weight_db) return;

        size_t n = fft_result.size();
        double freq_resolution = static_cast<double>(sample_rate) / static_cast<double>(n);

        for (size_t i = 0; i < n / 2; ++i) {
            double freq = static_cast<double>(i) * freq_resolution;
            double db = get_weight_db(freq, *weight_db);
            double factor = std::pow(10.0, db / 20.0);

            fft_result[i] *= factor;
            if (i > 0) {
                fft_result[n - i] *= factor;
            }
        }
    }

    void apply_weighting_time_domain(DVector& signal, uint32_t sample_rate, WeightType wtype)
    {
        if (wtype == WeightType::None || signal.empty() || sample_rate == 0) return;

        const std::vector<double>* weight_db = get_weight_table(wtype);
        if (!weight_db) return;

        size_t originalSize = signal.size();
        size_t nfft = 1;
        while (nfft < originalSize) nfft <<= 1;

        CVector spectrum(nfft, Complex(0.0, 0.0));
        for (size_t i = 0; i < originalSize; ++i) {
            spectrum[i] = Complex(signal[i], 0.0);
        }

        FFTCore::fft_base2(spectrum, false);

        const double freq_resolution = static_cast<double>(sample_rate) / static_cast<double>(nfft);

        for (size_t i = 0; i <= nfft / 2; ++i) {
            const double freq = static_cast<double>(i) * freq_resolution;
            const double db = get_weight_db(freq, *weight_db);
            const double factor = std::pow(10.0, db / 20.0);

            spectrum[i] *= factor;
            if (i > 0 && i < nfft / 2) {
                spectrum[nfft - i] *= factor;
            }
        }

        FFTCore::fft_base2(spectrum, true);

        for (size_t i = 0; i < originalSize; ++i) {
            signal[i] = spectrum[i].real();
        }
    }

    std::string weight_type_to_string(WeightType wtype) {
        switch (wtype) {
        case WeightType::None: return "无计权";
        case WeightType::A:    return "A计权";
        case WeightType::B:    return "B计权";
        case WeightType::C:    return "C计权";
        case WeightType::D:    return "D计权";
        default:               return "未知计权";
        }
    }

} // namespace Weighting