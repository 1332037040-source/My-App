#pragma once
#include <string>
#include <vector>

struct ATFXChannelInfo {
    std::string channelName;
    std::string dataType;
    size_t dataLength = 0;
    size_t dataOffset = 0;
    std::string channelLabel;

    // 新增真实字段
    std::string unit;   // 从 MeaQ->UnitId->Unit.Name
    std::string dof;    // 从 MeaQ->ParamSet(Channel-Tags)->Param(Name=DOF).Value
};

class FFT11_ATFXReader {
public:
    bool GetAllChannels(const std::string& atfxPath, std::vector<ATFXChannelInfo>& outChannels, double& sampleRate);
    bool ReadChannelData(const std::string& atfxPath, const std::string& channelName, std::vector<float>& outData, double& sampleRate);
};