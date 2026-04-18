#pragma once
#include "core/common.h"

namespace Utils {
    std::string get_timestamp();
    size_t next_power_of_2(size_t n);
    std::string get_unique_path(const std::string& p, const std::string& ext);
}
