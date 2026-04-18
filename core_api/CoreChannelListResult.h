#pragma once

#include <string>
#include <vector>
#include "core_api/CoreChannelInfo.h"

struct CoreChannelListResult
{
    bool success = false;
    std::string message;

    std::string filePath;
    std::string fileFormat;

    std::vector<CoreChannelInfo> channels;
};