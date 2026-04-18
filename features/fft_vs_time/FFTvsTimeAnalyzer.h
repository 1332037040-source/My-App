#pragma once
#include "core/common.h"
#include "domain/Types.h"
#include "domain/Spectrogram.h"

namespace FFTvsTimeAnalyzer {
    Spectrogram Compute(
        const DVector& x,
        double fs,
        const FFTParams& p
    );
}
