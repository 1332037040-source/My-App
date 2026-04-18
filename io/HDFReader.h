#pragma once
#include <string>
#include <vector>
#include <cstddef>

struct HDFChannelInfo {
    std::string channelName;
    std::string dataType;      // FLOAT32 / COMPLEX_FLOAT32 / UNKNOWN
    size_t dataLength = 0;     // 每通道点数（时域=采样点，频域=频率点）
    size_t dataOffset = 0;     // 数据区起始偏移（start of data）
    std::string channelLabel;

    std::string unit;          // physical unit
    std::string dof;           // 兼容字段，HDF通常无DOF
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

    bool ReadChannelDataByIndex(const std::string& hdfPath,
        size_t channelIndex,
        std::vector<float>& outData,
        double& sampleRate);
};
