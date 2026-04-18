#include "ATFXReader.h"
#include "pugixml.hpp"
#include <fstream>
#include <iostream>
#include <algorithm>
#include <stdexcept>
#include <sstream>
#include <cctype>
#include <cstring>
#include <unordered_map>
#include <unordered_set>

template <typename T>
T safe_ston(const std::string& str, const std::string& field, const std::string& context, T defaultValue = 0) {
    try {
        if constexpr (std::is_same<T, size_t>::value)
            return static_cast<size_t>(std::stoull(str));
        else if constexpr (std::is_same<T, int>::value)
            return std::stoi(str);
        else if constexpr (std::is_same<T, double>::value)
            return std::stod(str);
        else
            static_assert(!sizeof(T*), "unsupported type!");
    }
    catch (const std::exception& e) {
        std::cerr << "ATFX [" << context << "] 字段[" << field << "] 解析异常，原始内容为[" << str << "]，异常:" << e.what() << std::endl;
        return defaultValue;
    }
}

static std::string Trim(const std::string& s) {
    auto is_not_space = [](unsigned char c) { return !std::isspace(c); };
    auto b = std::find_if(s.begin(), s.end(), is_not_space);
    if (b == s.end()) return "";
    auto e = std::find_if(s.rbegin(), s.rend(), is_not_space).base();
    return std::string(b, e);
}

// 将 "1 2 3" 拆成 id 列表
static std::vector<std::string> SplitIds(const std::string& s) {
    std::vector<std::string> out;
    std::stringstream ss(s);
    std::string tok;
    while (ss >> tok) out.push_back(tok);
    return out;
}

static std::string GetBinFilePath(const std::string& atfxPath, const pugi::xml_node& filesNode) {
    auto comp = filesNode.child("component");
    if (comp) {
        auto binFile = comp.child_value("filename");
        if (binFile && strlen(binFile) > 0) {
            auto dirSep = atfxPath.find_last_of("/\\");
            if (dirSep != std::string::npos) {
                std::string dir = atfxPath.substr(0, dirSep + 1);
                return dir + binFile;
            }
            else {
                return binFile;
            }
        }
    }
    return atfxPath.substr(0, atfxPath.find_last_of('.')) + ".atfbin";
}

static double GetSampleRate(const pugi::xml_node& instance_data) {
    for (auto localCol : instance_data.children("LocalCol")) {
        if (std::atoi(localCol.child_value("Independent")) == 1) {
            std::string repr = localCol.child_value("Representation");
            if (repr == "implicit_linear") {
                double x0 = 0, dx = 0;
                std::stringstream ss(localCol.child_value("GenerationParameters"));
                ss >> x0 >> dx;
                if (dx > 0) return 1.0 / dx;
            }
        }
    }
    return 0.0;
}

// 构建 UnitId -> UnitName
static std::unordered_map<std::string, std::string> BuildUnitMap(const pugi::xml_node& instance_data) {
    std::unordered_map<std::string, std::string> m;
    for (auto u : instance_data.children("Unit")) {
        std::string id = Trim(u.child_value("Id"));
        std::string name = Trim(u.child_value("Name"));
        if (!id.empty()) m[id] = name;
    }
    return m;
}

// 构建 ParamSetId -> 是否是 Channel-Tags
static std::unordered_set<std::string> BuildChannelTagParamSetIds(const pugi::xml_node& instance_data) {
    std::unordered_set<std::string> ids;
    for (auto ps : instance_data.children("ParamSet")) {
        std::string id = Trim(ps.child_value("Id"));
        std::string name = Trim(ps.child_value("Name"));
        std::string desc = Trim(ps.child_value("Description"));

        // 你的文件里是 "_HEAD_ParameterSet_Channel-Tags_xxx" 且 Description=Channel-Tags
        if (!id.empty() &&
            (name.find("Channel-Tags") != std::string::npos || desc == "Channel-Tags")) {
            ids.insert(id);
        }
    }
    return ids;
}

// 构建 ParamSetId -> (ParamName -> ParamValue)
static std::unordered_map<std::string, std::unordered_map<std::string, std::string>>
BuildParamSetParamMap(const pugi::xml_node& instance_data) {
    // 先 ParamId -> (Name,Value)
    std::unordered_map<std::string, std::pair<std::string, std::string>> paramById;
    for (auto p : instance_data.children("Param")) {
        std::string id = Trim(p.child_value("Id"));
        if (id.empty()) continue;
        std::string n = Trim(p.child_value("Name"));
        std::string v = Trim(p.child_value("Value"));
        paramById[id] = { n, v };
    }

    // 再 ParamSetId -> map
    std::unordered_map<std::string, std::unordered_map<std::string, std::string>> out;
    for (auto ps : instance_data.children("ParamSet")) {
        std::string psid = Trim(ps.child_value("Id"));
        if (psid.empty()) continue;

        auto ids = SplitIds(ps.child_value("ParamId"));
        for (const auto& pid : ids) {
            auto it = paramById.find(pid);
            if (it == paramById.end()) continue;
            const auto& key = it->second.first;
            const auto& val = it->second.second;
            if (!key.empty()) out[psid][key] = val;
        }
    }
    return out;
}

// 构建 MeaQId -> (UnitId, ParamSetIds...)
struct MeaQMeta {
    std::string unitId;
    std::vector<std::string> paramSetIds;
};
static std::unordered_map<std::string, MeaQMeta> BuildMeaQMap(const pugi::xml_node& instance_data) {
    std::unordered_map<std::string, MeaQMeta> m;
    for (auto q : instance_data.children("MeaQ")) {
        std::string id = Trim(q.child_value("Id"));
        if (id.empty()) continue;
        MeaQMeta meta;
        meta.unitId = Trim(q.child_value("UnitId"));
        meta.paramSetIds = SplitIds(q.child_value("ParamSetId"));
        m[id] = std::move(meta);
    }
    return m;
}

bool FFT11_ATFXReader::GetAllChannels(const std::string& atfxPath, std::vector<ATFXChannelInfo>& outChannels, double& sampleRate) {
    pugi::xml_document doc;
    if (!doc.load_file(atfxPath.c_str())) {
        std::cerr << "[错误] 无法打开ATFX文件: " << atfxPath << std::endl;
        return false;
    }

    // 注意：你的xml有默认命名空间，pugi按本地名匹配通常可用
    auto root = doc.child("atfx_file");
    auto instance_data = root.child("instance_data");

    outChannels.clear();
    sampleRate = GetSampleRate(instance_data);

    auto unitMap = BuildUnitMap(instance_data);
    auto channelTagPsIds = BuildChannelTagParamSetIds(instance_data);
    auto paramSetParamMap = BuildParamSetParamMap(instance_data);
    auto meaQMap = BuildMeaQMap(instance_data);

    for (auto lc : instance_data.children("LocalCol")) {
        if (std::atoi(lc.child_value("Independent")) != 0) continue; // 只读信号轴

        ATFXChannelInfo ch;
        ch.channelName = lc.child_value("Name");
        ch.channelLabel = ch.channelName;

        auto comp = lc.child("Values").child("component");
        if (!comp) continue;

        ch.dataType = comp.child_value("datatype");
        ch.dataLength = safe_ston<size_t>(comp.child_value("length"), "length", ch.channelName, 0);
        ch.dataOffset = safe_ston<size_t>(comp.child_value("inioffset"), "inioffset", ch.channelName, 0);

        if (ch.dataLength == 0) continue;

        // === 关键：通过 MeaQId 找 Unit/Dof ===
        std::string meaQId = Trim(lc.child_value("MeaQId"));
        ch.unit = "-";
        ch.dof = "-";

        auto mqIt = meaQMap.find(meaQId);
        if (mqIt != meaQMap.end()) {
            // Unit: MeaQ.UnitId -> Unit.Name
            const std::string& uid = mqIt->second.unitId;
            auto uIt = unitMap.find(uid);
            if (uIt != unitMap.end() && !uIt->second.empty()) {
                ch.unit = uIt->second;
            }

            // Dof: 优先在 Channel-Tags 参数集中找 Param(Name=DOF)
            for (const auto& psid : mqIt->second.paramSetIds) {
                if (channelTagPsIds.find(psid) == channelTagPsIds.end()) continue;
                auto psmIt = paramSetParamMap.find(psid);
                if (psmIt == paramSetParamMap.end()) continue;

                auto dofIt = psmIt->second.find("DOF");
                if (dofIt != psmIt->second.end() && !dofIt->second.empty()) {
                    ch.dof = dofIt->second;
                    break;
                }
            }

            // 某些通道没有 DOF 参数，保持 "-"
        }

        outChannels.push_back(ch);
    }

    return !outChannels.empty();
}

bool FFT11_ATFXReader::ReadChannelData(const std::string& atfxPath, const std::string& channelName, std::vector<float>& outData, double& sampleRate) {
    pugi::xml_document doc;
    if (!doc.load_file(atfxPath.c_str())) {
        std::cerr << "[错误] 无法打开ATFX文件: " << atfxPath << std::endl;
        return false;
    }

    auto root = doc.child("atfx_file");
    auto instance_data = root.child("instance_data");
    auto filesNode = root.child("files");
    sampleRate = GetSampleRate(instance_data);

    for (auto lc : instance_data.children("LocalCol")) {
        if (std::atoi(lc.child_value("Independent")) != 0) continue;
        std::string thisName = lc.child_value("Name");
        if (thisName != channelName) continue;

        auto comp = lc.child("Values").child("component");
        if (!comp) {
            std::cerr << "[错误] ATFX通道节点缺少component: " << channelName << std::endl;
            return false;
        }

        std::string dtype = comp.child_value("datatype");
        size_t length = safe_ston<size_t>(comp.child_value("length"), "length", channelName, 0);
        size_t offset = safe_ston<size_t>(comp.child_value("inioffset"), "inioffset", channelName, 0);

        if (dtype != "ieeefloat4") {
            std::cerr << "[错误] 暂仅支持ieeefloat4格式: 当前为 " << dtype << std::endl;
            return false;
        }
        if (length == 0) {
            std::cerr << "[错误] 通道[" << channelName << "]数据长度为0！" << std::endl;
            return false;
        }

        std::string binFilePath = GetBinFilePath(atfxPath, filesNode);
        std::ifstream fin(binFilePath, std::ios::binary);
        if (!fin) {
            std::cerr << "[错误] 无法打开bin文件: " << binFilePath << std::endl;
            return false;
        }

        fin.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
        outData.resize(length);
        fin.read(reinterpret_cast<char*>(outData.data()), static_cast<std::streamsize>(sizeof(float) * length));
        if (!fin) {
            std::cerr << "[错误] bin文件读取失败: " << binFilePath << std::endl;
            return false;
        }
        return true;
    }

    std::cerr << "[错误] 未找到指定通道: " << channelName << std::endl;
    return false;
}
