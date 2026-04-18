#pragma once
#include "core/common.h"

namespace AudioBlocking {
    std::vector<DVector> split_audio_into_blocks(const DVector& data, size_t block_size, double overlap);
}
