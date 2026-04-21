#pragma once
#include <string>
#include <vector>
#include <cstddef>

struct HDFChannelInfo {
    std::string channelName;
    std::string channelLabel;
    std::string dataType;
    size_t dataLength = 0;
    size_t dataOffset = 0;
    std::string unit;
    std::string dof;

    // 兼容旧逻辑：对外主采样率（建议放工程层effective）
    double sampleRate = 0.0;
    bool sampleRateTrusted = false;

    // 新增：解析层（文件内部时基）
    double internalSampleRate = 0.0;
    bool internalSampleRateTrusted = false;

    // 新增：业务层（UI/算法使用）
    double effectiveSampleRate = 0.0;
    bool effectiveSampleRateTrusted = false;
};

class FFT11_HDFReader {
public:
    bool GetAllChannels(const std::string& hdfPath,
        std::vector<HDFChannelInfo>& outChannels,
        double& sampleRate);

    bool ReadChannelData(const std::string& hdfPath,
        const std::string& channelName,
        std::vector<float>& outData,
        double& sampleRate);

    bool ReadChannelDataWithFsQuality(const std::string& hdfPath,
        const std::string& channelName,
        std::vector<float>& outData,
        double& sampleRate,
        bool& sampleRateTrusted);
};