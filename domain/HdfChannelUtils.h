#pragma once

#include "../io/ATFXReader.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace HdfChannelUtils {
inline std::string ToLowerCopy(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

inline bool ContainsTokenInsensitive(const std::string& text, const std::string& token) {
    return ToLowerCopy(text).find(ToLowerCopy(token)) != std::string::npos;
}

inline std::string DetectRpmChannelName(const std::vector<ATFXChannelInfo>& channels) {
    for (const auto& ch : channels) {
        if (ContainsTokenInsensitive(ch.unit, "rpm")) {
            return ch.channelName;
        }
    }
    for (const auto& ch : channels) {
        if (ContainsTokenInsensitive(ch.channelName, "rpm") ||
            ContainsTokenInsensitive(ch.channelLabel, "rpm")) {
            return ch.channelName;
        }
    }
    return "";
}
}
