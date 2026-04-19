#include "AngleResampler.h"
#include <cmath>
#include <algorithm>

namespace OrderTracking {

    bool ResampleByAngle(
        const std::vector<double>& x,
        const std::vector<double>& rpm,
        double fs,
        double dTheta,
        std::vector<double>& xTheta,
        std::vector<double>& tTheta)
    {
        xTheta.clear(); tTheta.clear();
        if (x.size() < 2 || rpm.size() != x.size() || fs <= 0 || dTheta <= 0) return false;

        const double dt = 1.0 / fs;
        const size_t N = x.size();

        std::vector<double> theta(N, 0.0);
        for (size_t i = 1; i < N; ++i) {
            const double w = 2.0 * M_PI * std::max(0.0, rpm[i]) / 60.0;
            theta[i] = theta[i - 1] + w * dt;
        }

        const double th0 = theta.front();
        const double th1 = theta.back();
        if (th1 <= th0 + dTheta) return false;

        size_t j = 0;
        for (double th = th0; th <= th1; th += dTheta) {
            while (j + 1 < N && theta[j + 1] < th) ++j;
            if (j + 1 >= N) break;

            const double thA = theta[j], thB = theta[j + 1];
            const double a = (thB > thA) ? (th - thA) / (thB - thA) : 0.0;

            const double xv = x[j] * (1.0 - a) + x[j + 1] * a;
            const double tv = (double)j * dt + a * dt;

            xTheta.push_back(xv);
            tTheta.push_back(tv);
        }

        return !xTheta.empty();
    }

}