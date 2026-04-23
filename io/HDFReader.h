#pragma once

#include <string>
#include <vector>
#include <cstddef>

struct HDFChannelInfo {
    // 通道基本信息
    std::string channelName;
    std::string channelLabel;
    std::string dataType;
    std::string unit;
    std::string dof;

    // 数据区信息
    size_t dataLength = 0;
    size_t dataOffset = 0;

    // 对外主采样率字段
    double sampleRate = 0.0;
    bool sampleRateTrusted = false;

    // 兼容/调试字段
    double internalSampleRate = 0.0;
    bool internalSampleRateTrusted = false;

    double effectiveSampleRate = 0.0;
    bool effectiveSampleRateTrusted = false;
};

class FFT11_HDFReader {
public:
    // 读取文件中的所有通道信息
    // sampleRate 返回文件级基础频率 Fbase = 1 / delta value
    bool GetAllChannels(const std::string& hdfPath,
        std::vector<HDFChannelInfo>& outChannels,
        double& sampleRate);

    // 读取指定通道数据
    bool ReadChannelData(const std::string& hdfPath,
        const std::string& channelName,
        std::vector<float>& outData,
        double& sampleRate);

    // 读取指定通道数据，并返回采样率可信标记
    bool ReadChannelDataWithFsQuality(const std::string& hdfPath,
        const std::string& channelName,
        std::vector<float>& outData,
        double& sampleRate,
        bool& sampleRateTrusted);
};