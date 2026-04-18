#pragma once
#include "core/common.h"

namespace FFTCore {
    void fft_base2(CVector& x, bool invert);
    CVector fft(CVector x);
}
