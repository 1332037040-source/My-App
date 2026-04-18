#include "Utils.h"
#include <sstream>
#include <iomanip>
#include <chrono>
#include <ctime>

namespace Utils {
    std::string get_timestamp() {
        auto now = std::chrono::system_clock::now();
        auto in_time_t = std::chrono::system_clock::to_time_t(now);
        std::stringstream ss;
        ss << std::put_time(std::localtime(&in_time_t), "%Y%m%d_%H%M%S");
        return ss.str();
    }

    size_t next_power_of_2(size_t n) {
        if (n <= 1) return 1;
        size_t pow2 = 1;
        while (pow2 < n) pow2 <<= 1;
        return pow2;
    }

    std::string get_unique_path(const std::string& p, const std::string& ext) {
        size_t dot = p.find_last_of('.');
        size_t slash = p.find_last_of("\\/");
        std::string name = (slash != std::string::npos) ? p.substr(slash + 1, dot - slash - 1) : p.substr(0, dot);
        std::string dir = (slash != std::string::npos) ? p.substr(0, slash + 1) : "";
        return dir + name + "_" + get_timestamp() + ext;
    }
}
