#include "AudioBlocking.h"
#include <iostream>

namespace AudioBlocking {
    std::vector<DVector> split_audio_into_blocks(const DVector& data, size_t block_size, double overlap) {
        std::vector<DVector> blocks;
        if (data.empty() || block_size == 0 || overlap < 0 || overlap >= 1) {
            std::cerr << "警告：分块参数无效！" << std::endl;
            return blocks;
        }
        size_t step = static_cast<size_t>(block_size * (1 - overlap));
        if (step == 0) step = 1;
        for (size_t s = 0; s + block_size <= data.size(); s += step) {
            blocks.emplace_back(data.begin() + s, data.begin() + s + block_size);
        }
        std::cout << "成功分块：总块数 = " << blocks.size() << "，块长度 = " << block_size << std::endl;
        return blocks;
    }
}
