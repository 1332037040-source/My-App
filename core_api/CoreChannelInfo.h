#pragma once

#include <string>

struct CoreChannelInfo
{
    int id = -1;
    std::string name;
    std::string unit;
    double sampleRate = 0.0;
    size_t sampleCount = 0;
    double durationSec = 0.0;
    std::string dof;
};