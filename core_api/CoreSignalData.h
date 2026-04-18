#pragma once

#include <string>
#include <vector>

struct CoreSignalData
{
    bool success = false;
    std::string message;

    std::string filePath;
    std::string channelName;
    std::string unit;

    double sampleRate = 0.0;
    size_t totalSampleCount = 0;

    size_t readStartIndex = 0;
    size_t readSampleCount = 0;

    std::vector<double> samples;
};