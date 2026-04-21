#include "HDFReader.h"

#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <cctype>
#include <limits>
#include <cstring>
#include <cmath>
#include <type_traits>
#include <vector>

namespace {

    // ========================= 基础工具 =========================
    static std::string Trim(const std::string& s) {
        auto is_not_space = [](unsigned char c) { return !std::isspace(c); };
        auto b = std::find_if(s.begin(), s.end(), is_not_space);
        if (b == s.end()) return "";
        auto e = std::find_if(s.rbegin(), s.rend(), is_not_space).base();
        return std::string(b, e);
    }

    static std::string ToLowerCopy(std::string s) {
        std::transform(s.begin(), s.end(), s.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    }

    static std::string NormalizeKey(const std::string& keyRaw) {
        std::string k = ToLowerCopy(Trim(keyRaw));
        std::string out;
        out.reserve(k.size());
        bool prevSpace = false;
        for (char c : k) {
            if (std::isspace(static_cast<unsigned char>(c))) {
                if (!prevSpace) out.push_back(' ');
                prevSpace = true;
            }
            else {
                out.push_back(c);
                prevSpace = false;
            }
        }
        return Trim(out);
    }

    template<typename T>
    static T SafeParse(const std::string& s, const T& defVal) {
        try {
            if constexpr (std::is_same<T, int>::value) return static_cast<int>(std::stoll(s));
            if constexpr (std::is_same<T, size_t>::value) return static_cast<size_t>(std::stoull(s));
            if constexpr (std::is_same<T, float>::value) return std::stof(s);
            if constexpr (std::is_same<T, double>::value) return std::stod(s);
        }
        catch (...) {}
        return defVal;
    }

    static bool IsLittleEndianHost() {
        uint16_t x = 0x0102;
        auto* p = reinterpret_cast<unsigned char*>(&x);
        return p[0] == 0x02;
    }

    static inline uint32_t BSwap32(uint32_t x) {
        return ((x & 0x000000FFu) << 24) |
            ((x & 0x0000FF00u) << 8) |
            ((x & 0x00FF0000u) >> 8) |
            ((x & 0xFF000000u) >> 24);
    }

    static float ReadF32(const unsigned char* p, bool littleEndianFile) {
        uint32_t u = 0;
        std::memcpy(&u, p, sizeof(uint32_t));
        const bool littleHost = IsLittleEndianHost();
        if (littleHost != littleEndianFile) u = BSwap32(u);
        float f = 0.0f;
        std::memcpy(&f, &u, sizeof(float));
        return f;
    }

    // ========================= 元数据 =========================
    struct ChannelMeta {
        std::string name = "Channel";
        std::string unit = "-";
        std::string label = "";
        std::string quantity;
        std::string dof = "-";
    };

    struct HDFMeta {
        std::string kind;
        std::string byteOrder;
        std::string implementationType;
        std::string scanMode;
        std::string chOrderRaw;
        std::string dataOrgRaw;

        size_t startOfData = 65536;
        size_t nChannels = 1;
        size_t nScans = 0;

        double deltaValue = 0.0;
        double firstValue = 0.0;

        std::vector<ChannelMeta> channels;
    };

    struct ChOrderToken {
        size_t repeat = 1;
        size_t channel = 1; // 1-based
    };

    static bool ParseHeader(const std::string& path, HDFMeta& meta, std::string& err) {
        err.clear();
        meta = HDFMeta{};

        std::ifstream fin(path, std::ios::binary);
        if (!fin) { err = "无法打开HDF文件"; return false; }

        const size_t probeSize = 65536;
        std::vector<char> probe(probeSize, 0);
        fin.read(probe.data(), static_cast<std::streamsize>(probeSize));
        size_t got = static_cast<size_t>(fin.gcount());
        if (got == 0) { err = "HDF文件为空"; return false; }

        std::string probeText(probe.data(), probe.data() + got);
        {
            std::istringstream iss(probeText);
            std::string line;
            bool foundStart = false;
            while (std::getline(iss, line)) {
                std::string t = Trim(line);
                if (t.empty() || t[0] == ';') continue;
                auto pos = t.find(':');
                if (pos == std::string::npos) continue;
                std::string key = NormalizeKey(t.substr(0, pos));
                std::string val = Trim(t.substr(pos + 1));
                if (key == "start of data") {
                    meta.startOfData = SafeParse<size_t>(val, 65536);
                    foundStart = true;
                    break;
                }
            }
            if (!foundStart) meta.startOfData = 65536;
        }

        fin.clear();
        fin.seekg(0, std::ios::beg);
        std::vector<char> headerBytes(meta.startOfData, 0);
        fin.read(headerBytes.data(), static_cast<std::streamsize>(meta.startOfData));
        if (static_cast<size_t>(fin.gcount()) < meta.startOfData) {
            err = "头部长度不足start_of_data";
            return false;
        }

        std::string headerText(headerBytes.data(), headerBytes.data() + headerBytes.size());
        std::istringstream iss(headerText);

        std::string line;
        bool inChannelDef = false;
        ChannelMeta curCh;
        bool hasCurCh = false;

        while (std::getline(iss, line)) {
            std::string t = Trim(line);
            if (t.empty() || t[0] == ';') continue;

            auto pos = t.find(':');
            if (pos == std::string::npos) continue;

            std::string key = NormalizeKey(t.substr(0, pos));
            std::string val = Trim(t.substr(pos + 1));

            if (key == "channel definition") {
                if (hasCurCh) meta.channels.push_back(curCh);
                curCh = ChannelMeta{};
                hasCurCh = true;
                inChannelDef = true;
                continue;
            }
            if (key == "abscissa definition") {
                if (hasCurCh) {
                    meta.channels.push_back(curCh);
                    hasCurCh = false;
                }
                inChannelDef = false;
                continue;
            }

            if (key == "kind") meta.kind = ToLowerCopy(val);
            else if (key == "byte order") meta.byteOrder = ToLowerCopy(val);
            else if (key == "implementation type") meta.implementationType = ToLowerCopy(val);
            else if (key == "scan mode") meta.scanMode = ToLowerCopy(val);
            else if (key == "ch order") meta.chOrderRaw = val;
            else if (key == "data org") meta.dataOrgRaw = ToLowerCopy(val);
            else if (key == "nbr of channel") meta.nChannels = SafeParse<size_t>(val, 1);
            else if (key == "nbr of scans") meta.nScans = SafeParse<size_t>(val, 0);
            else if (key == "delta value") meta.deltaValue = SafeParse<double>(val, 0.0);
            else if (key == "first value") meta.firstValue = SafeParse<double>(val, 0.0);
            else if (inChannelDef && key == "name str") curCh.name = val.empty() ? "Channel" : val;
            else if (inChannelDef && key == "physical unit") curCh.unit = val.empty() ? "-" : val;
            else if (inChannelDef && key == "title str") curCh.label = val;
            else if (inChannelDef && key == "physical quantity") curCh.quantity = ToLowerCopy(val);
        }

        if (hasCurCh) meta.channels.push_back(curCh);

        if (meta.channels.size() < meta.nChannels) {
            size_t old = meta.channels.size();
            meta.channels.resize(meta.nChannels);
            for (size_t i = old; i < meta.nChannels; ++i) {
                meta.channels[i].name = "Channel " + std::to_string(i + 1);
                meta.channels[i].unit = "-";
                meta.channels[i].label = "";
                meta.channels[i].quantity = "";
                meta.channels[i].dof = "-";
            }
        }
        else if (meta.channels.size() > meta.nChannels) {
            meta.channels.resize(meta.nChannels);
        }

        // 扩展区读取 DOF
        {
            std::istringstream exss(headerText);
            std::string ln;
            int currentExtCh = -1;
            while (std::getline(exss, ln)) {
                std::string t = Trim(ln);
                if (t.empty()) continue;

                if (t.front() == '[' && t.back() == ']') {
                    std::string inner = t.substr(1, t.size() - 2);
                    std::string low = ToLowerCopy(inner);
                    if (low.rfind("channel", 0) == 0) {
                        std::string idxStr = inner.substr(7);
                        currentExtCh = SafeParse<int>(idxStr, -1);
                    }
                    else {
                        currentExtCh = -1;
                    }
                    continue;
                }

                if (currentExtCh < 0) continue;

                auto p = t.find('=');
                if (p == std::string::npos) continue;

                std::string k = ToLowerCopy(Trim(t.substr(0, p)));
                std::string v = Trim(t.substr(p + 1));

                if (k == "dof") {
                    auto l = v.find('(');
                    auto r = v.rfind(')');
                    std::string val = (l != std::string::npos && r != std::string::npos && r > l)
                        ? Trim(v.substr(l + 1, r - l - 1))
                        : Trim(v);

                    if (currentExtCh >= 0 && static_cast<size_t>(currentExtCh) < meta.channels.size()) {
                        if (!val.empty()) meta.channels[currentExtCh].dof = val;
                    }
                }
            }
        }

        return true;
    }

    static bool IsTimeKind(const HDFMeta& m) { return m.kind.find("time data") != std::string::npos; }
    static bool IsComplexKind(const HDFMeta& m) {
        if (m.implementationType.find("complex") != std::string::npos) return true;
        if (m.kind.find("transfer") != std::string::npos) return true;
        if (m.kind.find("auto") != std::string::npos) return true;
        if (m.kind.find("coh") != std::string::npos) return true;
        return false;
    }
    static bool IsLittleEndianFile(const HDFMeta& m) { return m.byteOrder.find("intel") != std::string::npos; }
    static bool IsSimultaneous(const HDFMeta& m) {
        return ToLowerCopy(m.scanMode).find("simultaneous") != std::string::npos;
    }

    // 裸数字按 channel 解析（repeat=1）
    static bool ParseChOrderTokens(const HDFMeta& meta, std::vector<ChOrderToken>& out) {
        out.clear();
        if (meta.chOrderRaw.empty()) return false;

        std::string s = meta.chOrderRaw;
        for (char& c : s) if (c == ',' || c == ';' || c == '\t') c = ' ';

        std::stringstream ss(s);
        std::string tok;

        while (ss >> tok) {
            tok = Trim(tok);
            if (tok.empty()) continue;

            auto p = tok.find('*');
            if (p != std::string::npos) {
                size_t rep = SafeParse<size_t>(tok.substr(0, p), 0);
                size_t ch = SafeParse<size_t>(tok.substr(p + 1), 0);
                if (rep == 0 || ch == 0) continue;
                out.push_back({ rep, ch });
            }
            else {
                size_t ch = SafeParse<size_t>(tok, 0);
                if (ch == 0) continue;
                out.push_back({ 1, ch });
            }
        }
        return !out.empty();
    }

    static bool BuildRepPerChannel(const HDFMeta& meta, std::vector<size_t>& repPerCh) {
        repPerCh.assign(meta.nChannels, 0);

        std::vector<ChOrderToken> tokens;
        if (!ParseChOrderTokens(meta, tokens)) return false;

        for (const auto& t : tokens) {
            if (t.channel >= 1 && t.channel <= meta.nChannels) {
                repPerCh[t.channel - 1] += t.repeat;
            }
        }
        return true;
    }

    static double ComputeInternalFs(size_t rep, double delta) {
        if (delta <= 0.0 || rep == 0) return 0.0;
        return static_cast<double>(rep) / delta;
    }

    // 按 delta + repeat 自动推导 effectiveFs
    // Fbase = (1/delta) / maxRepeat
    // Fi = repeat_i * Fbase
    static bool ComputeEffectiveFsFromDeltaAndRepeat(
        const std::vector<size_t>& repPerCh,
        double delta,
        std::vector<double>& outEffectiveFs)
    {
        outEffectiveFs.clear();
        if (repPerCh.empty() || delta <= 0.0) return false;

        size_t rmax = 0;
        for (size_t r : repPerCh) rmax = std::max(rmax, r);
        if (rmax == 0) return false;

        double globalFs = 1.0 / delta;
        double fbase = globalFs / static_cast<double>(rmax);

        outEffectiveFs.resize(repPerCh.size(), 0.0);
        for (size_t i = 0; i < repPerCh.size(); ++i) {
            outEffectiveFs[i] = static_cast<double>(repPerCh[i]) * fbase;
        }
        return true;
    }

    static bool ExtractChannelData(const HDFMeta& meta,
        const std::vector<unsigned char>& raw,
        size_t targetCh,
        std::vector<float>& outData)
    {
        outData.clear();

        const bool littleFile = IsLittleEndianFile(meta);
        const bool isComplex = IsComplexKind(meta);
        const size_t bytesPerSample = isComplex ? sizeof(float) * 2 : sizeof(float);

        if (IsSimultaneous(meta) && meta.chOrderRaw.find('*') == std::string::npos) {
            const size_t expectedBytes = meta.nScans * meta.nChannels * bytesPerSample;
            if (raw.size() < expectedBytes) return false;

            outData.resize(meta.nScans);
            for (size_t i = 0; i < meta.nScans; ++i) {
                size_t idx = i * meta.nChannels + targetCh;
                const unsigned char* p = raw.data() + idx * bytesPerSample;
                if (!isComplex) outData[i] = ReadF32(p, littleFile);
                else {
                    float re = ReadF32(p, littleFile);
                    float im = ReadF32(p + sizeof(float), littleFile);
                    outData[i] = std::sqrt(re * re + im * im);
                }
            }
            return true;
        }

        std::vector<ChOrderToken> tokens;
        if (!ParseChOrderTokens(meta, tokens)) return false;

        size_t samplesPerBlockAll = 0;
        size_t targetPerBlock = 0;
        for (const auto& t : tokens) {
            samplesPerBlockAll += t.repeat;
            if (t.channel >= 1 && t.channel <= meta.nChannels && (t.channel - 1) == targetCh) {
                targetPerBlock += t.repeat;
            }
        }
        if (samplesPerBlockAll == 0 || targetPerBlock == 0) return false;

        const size_t bytesPerBlock = samplesPerBlockAll * bytesPerSample;
        if (bytesPerBlock == 0 || raw.size() < bytesPerBlock) return false;

        const size_t nBlocks = raw.size() / bytesPerBlock;
        outData.reserve(nBlocks * targetPerBlock);

        for (size_t b = 0; b < nBlocks; ++b) {
            const unsigned char* blockBase = raw.data() + b * bytesPerBlock;
            size_t offsetSamples = 0;

            for (const auto& t : tokens) {
                size_t ch0 = t.channel - 1;
                for (size_t r = 0; r < t.repeat; ++r) {
                    if (ch0 == targetCh) {
                        const unsigned char* p = blockBase + (offsetSamples + r) * bytesPerSample;
                        if (!isComplex) outData.push_back(ReadF32(p, littleFile));
                        else {
                            float re = ReadF32(p, littleFile);
                            float im = ReadF32(p + sizeof(float), littleFile);
                            outData.push_back(std::sqrt(re * re + im * im));
                        }
                    }
                }
                offsetSamples += t.repeat;
            }
        }

        return !outData.empty();
    }

} // namespace

bool FFT11_HDFReader::GetAllChannels(const std::string& hdfPath,
    std::vector<HDFChannelInfo>& outChannels,
    double& sampleRate)
{
    outChannels.clear();
    sampleRate = 0.0;

    HDFMeta meta;
    std::string err;
    if (!ParseHeader(hdfPath, meta, err)) {
        std::cerr << "[错误] HDF头解析失败: " << err << " | " << hdfPath << std::endl;
        return false;
    }

    if (!IsTimeKind(meta) || meta.deltaValue <= 0.0 || meta.nChannels == 0) {
        std::cerr << "[错误] 非时域或delta无效: " << hdfPath << std::endl;
        return false;
    }

    const bool simultaneous = IsSimultaneous(meta);
    const double globalFs = 1.0 / meta.deltaValue;
    sampleRate = simultaneous ? globalFs : 0.0;

    std::vector<size_t> repPerCh;
    if (!BuildRepPerChannel(meta, repPerCh)) repPerCh.assign(meta.nChannels, 1);

    std::vector<double> effectiveFsVec;
    bool hasEffective = ComputeEffectiveFsFromDeltaAndRepeat(repPerCh, meta.deltaValue, effectiveFsVec);

    outChannels.reserve(meta.nChannels);
    for (size_t i = 0; i < meta.nChannels; ++i) {
        HDFChannelInfo ch;
        ch.channelName = meta.channels[i].name.empty() ? ("Channel " + std::to_string(i + 1)) : meta.channels[i].name;
        ch.channelLabel = meta.channels[i].label.empty() ? ch.channelName : meta.channels[i].label;
        ch.dataType = IsComplexKind(meta) ? "COMPLEX_FLOAT32" : "FLOAT32";
        ch.dataLength = meta.nScans;
        ch.dataOffset = meta.startOfData;
        ch.unit = meta.channels[i].unit.empty() ? "-" : meta.channels[i].unit;
        ch.dof = meta.channels[i].dof.empty() ? "-" : meta.channels[i].dof;

        size_t rep = (i < repPerCh.size() ? repPerCh[i] : 1);
        if (rep == 0) rep = 1;

        double internalFs = ComputeInternalFs(rep, meta.deltaValue);
        bool internalTrusted = (internalFs > 0.0 && std::isfinite(internalFs));

        double effectiveFs = 0.0;
        bool effectiveTrusted = false;
        if (hasEffective && i < effectiveFsVec.size()) {
            effectiveFs = effectiveFsVec[i];
            effectiveTrusted = (effectiveFs > 0.0 && std::isfinite(effectiveFs));
        }

        ch.sampleRate = effectiveFs;
        ch.sampleRateTrusted = effectiveTrusted;

        ch.internalSampleRate = internalFs;
        ch.internalSampleRateTrusted = internalTrusted;
        ch.effectiveSampleRate = effectiveFs;
        ch.effectiveSampleRateTrusted = effectiveTrusted;

        outChannels.push_back(std::move(ch));
    }

    std::cout << "[DEBUG] HDF GetAllChannels:"
        << " path=" << hdfPath
        << ", scanMode=" << meta.scanMode
        << ", nChannels=" << meta.nChannels
        << ", nScans=" << meta.nScans
        << ", delta=" << meta.deltaValue
        << ", globalFs=" << globalFs
        << ", chOrderRaw=" << meta.chOrderRaw
        << ", dataOrg=" << meta.dataOrgRaw
        << std::endl;

    for (size_t i = 0; i < outChannels.size(); ++i) {
        std::cout << "  [DEBUG] ch" << (i + 1)
            << " name=" << outChannels[i].channelName
            << " unit=" << outChannels[i].unit
            << " dof=" << outChannels[i].dof
            << " internalFs=" << outChannels[i].internalSampleRate
            << " effectiveFs=" << outChannels[i].effectiveSampleRate
            << std::endl;
    }

    return !outChannels.empty();
}

bool FFT11_HDFReader::ReadChannelData(const std::string& hdfPath,
    const std::string& channelName,
    std::vector<float>& outData,
    double& sampleRate)
{
    bool trusted = false;
    return ReadChannelDataWithFsQuality(hdfPath, channelName, outData, sampleRate, trusted);
}

bool FFT11_HDFReader::ReadChannelDataWithFsQuality(const std::string& hdfPath,
    const std::string& channelName,
    std::vector<float>& outData,
    double& sampleRate,
    bool& sampleRateTrusted)
{
    outData.clear();
    sampleRate = 0.0;
    sampleRateTrusted = false;

    HDFMeta meta;
    std::string err;
    if (!ParseHeader(hdfPath, meta, err)) {
        std::cerr << "[错误] HDF头解析失败: " << err << " | " << hdfPath << std::endl;
        return false;
    }

    if (meta.nChannels == 0 || meta.nScans == 0 || meta.deltaValue <= 0.0) {
        std::cerr << "[错误] HDF元数据无效: nChannels=" << meta.nChannels
            << ", nScans=" << meta.nScans
            << ", delta=" << meta.deltaValue << std::endl;
        return false;
    }

    size_t targetCh = std::numeric_limits<size_t>::max();
    for (size_t i = 0; i < meta.channels.size(); ++i) {
        if (meta.channels[i].name == channelName) {
            targetCh = i;
            break;
        }
    }
    if (targetCh == std::numeric_limits<size_t>::max()) {
        for (size_t i = 0; i < meta.nChannels; ++i) {
            if (("Channel " + std::to_string(i + 1)) == channelName) {
                targetCh = i;
                break;
            }
        }
    }
    if (targetCh == std::numeric_limits<size_t>::max()) {
        std::cerr << "[错误] 未找到指定通道: " << channelName << std::endl;
        return false;
    }

    std::ifstream fin(hdfPath, std::ios::binary);
    if (!fin) {
        std::cerr << "[错误] 无法打开HDF文件: " << hdfPath << std::endl;
        return false;
    }

    fin.seekg(0, std::ios::end);
    std::streamoff fsize = fin.tellg();
    if (fsize <= static_cast<std::streamoff>(meta.startOfData)) {
        std::cerr << "[错误] 文件长度小于start_of_data" << std::endl;
        return false;
    }

    size_t binSize = static_cast<size_t>(fsize - static_cast<std::streamoff>(meta.startOfData));
    std::vector<unsigned char> raw(binSize);
    fin.seekg(static_cast<std::streamoff>(meta.startOfData), std::ios::beg);
    fin.read(reinterpret_cast<char*>(raw.data()), static_cast<std::streamsize>(binSize));
    if (!fin) {
        std::cerr << "[错误] 二进制区读取失败" << std::endl;
        return false;
    }

    if (!ExtractChannelData(meta, raw, targetCh, outData) || outData.empty()) {
        std::cerr << "[错误] 通道数据解析失败: " << channelName << std::endl;
        return false;
    }

    std::vector<size_t> repPerCh;
    if (!BuildRepPerChannel(meta, repPerCh)) repPerCh.assign(meta.nChannels, 1);

    std::vector<double> effectiveFsVec;
    bool hasEffective = ComputeEffectiveFsFromDeltaAndRepeat(repPerCh, meta.deltaValue, effectiveFsVec);

    if (hasEffective && targetCh < effectiveFsVec.size()) {
        sampleRate = effectiveFsVec[targetCh];
        sampleRateTrusted = (sampleRate > 0.0 && std::isfinite(sampleRate));
    }
    else {
        size_t rep = (targetCh < repPerCh.size() ? repPerCh[targetCh] : 1);
        sampleRate = ComputeInternalFs(rep == 0 ? 1 : rep, meta.deltaValue);
        sampleRateTrusted = false;
    }

    double internalFs = 0.0;
    {
        size_t rep = (targetCh < repPerCh.size() ? repPerCh[targetCh] : 1);
        internalFs = ComputeInternalFs(rep == 0 ? 1 : rep, meta.deltaValue);
    }

    std::cout << "[DEBUG] HDF ReadChannelDataWithFsQuality:"
        << " channel=" << channelName
        << ", outData.size=" << outData.size()
        << ", internalFs=" << internalFs
        << ", effectiveFs=" << sampleRate
        << ", trusted=" << sampleRateTrusted
        << ", scanMode=" << meta.scanMode
        << ", chOrderRaw=" << meta.chOrderRaw
        << std::endl;

    return true;
}