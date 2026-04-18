#pragma once

#include <string>

struct CoreDataRequest
{
    std::string filePath;
    std::string channelName;

    size_t startSample = 0;
    size_t sampleCount = 0;   // 0 表示尽可能读取全部
    bool allowPartialRead = true;
};