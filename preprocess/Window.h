#pragma once
#include "core/common.h"

namespace Window {
    enum class WindowType {
        Rectangular = 0,
        Hanning,
        Blackman,
        Hamming,
        Bartlett,   // 新增：三角窗
        FlatTop     // 新增：平顶窗
    };

    DVector generate_window(WindowType type, size_t window_len);
}
