#pragma once

#include <string>

inline bool IsHdfExt(const std::string& ext) {
    return ext == "hdf" || ext == "h5" || ext == "hdf5";
}
