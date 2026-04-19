#pragma once
#include <vector>

namespace OrderTracking {
    // 输入: 等时采样信号x, 对应rpm(同长度), fs
    // 输出: 等角度采样信号xTheta, 以及每个角样本对应的时间tTheta
    bool ResampleByAngle(
        const std::vector<double>& x,
        const std::vector<double>& rpm,
        double fs,
        double dTheta, // 例如 2*pi/1024
        std::vector<double>& xTheta,
        std::vector<double>& tTheta
    );
}