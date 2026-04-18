#pragma once

#include <string>

struct CoreFileInfo
{
    bool success = false;
    std::string message;

    std::string filePath;
    std::string fileFormat;   // "wav" / "atfx" / "hdf" / "unknown"
    bool readable = false;
    size_t channelCount = 0;
};