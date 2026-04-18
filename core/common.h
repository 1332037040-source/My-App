#pragma once

#define _CRT_SECURE_NO_WARNINGS
#define _USE_MATH_DEFINES

#include <vector>
#include <complex>
#include <string>
#include <cstdint>

using Complex = std::complex<double>;//定义双精度复数类型（FFT 算法的核心数据类型）
using CVector = std::vector<Complex>;//复数向量（存储 FFT 输入 / 输出的复数序列）
using DVector = std::vector<double>;//双精度浮点向量（存储实数序列，如原始采样数据）