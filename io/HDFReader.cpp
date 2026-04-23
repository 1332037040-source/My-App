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
        if (IsLittleEndianHost() != littleEndianFile) u = BSwap32(u);
        float f = 0.0f;
        std::memcpy(&f, &u, sizeof(float));
        return f;
    }

    static uint32_t ReadU32(const unsigned char* p, bool littleEndianFile) {
        uint32_t u = 0;
        std::memcpy(&u, p, sizeof(uint32_t));
        if (IsLittleEndianHost() != littleEndianFile) u = BSwap32(u);
        return u;
    }

    static size_t BytesPerSampleFromImpl(const std::string& impl) {
        std::string t = ToLowerCopy(impl);
        if (t.find("uint32") != std::string::npos) return 4;
        if (t.find("float32") != std::string::npos) return 4;
        return 4;
    }

    // ========================= 元数据 =========================
    struct ChannelMeta {
        std::string name = "Channel";
        std::string unit = "-";
        std::string label = "";
        std::string quantity;
        std::string dof = "-";
        std::string implementationType = "FLOAT32";
    };

    struct HDFMeta {
        std::string kind;
        std::string byteOrder;
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

    struct ChannelRateRule {
        bool fixed1000 = false; // 单独数字 N
        size_t coeff = 0;       // X*N 中的 X
    };

    static bool IsTimeKind(const HDFMeta& m) {
        return ToLowerCopy(m.kind).find("time data") != std::string::npos;
    }

    static bool IsLittleEndianFile(const HDFMeta& m) {
        return ToLowerCopy(m.byteOrder).find("intel") != std::string::npos;
    }

    static bool IsSimultaneous(const HDFMeta& m) {
        return ToLowerCopy(m.scanMode).find("simultaneous") != std::string::npos &&
            ToLowerCopy(m.scanMode).find("multiple") == std::string::npos;
    }

    static bool IsSynchronisedMultiple(const HDFMeta& m) {
        return ToLowerCopy(m.scanMode).find("synchronised multiple") != std::string::npos;
    }

    // ========================= 头部解析 =========================
    static bool ParseHeader(const std::string& path, HDFMeta& meta, std::string& err) {
        err.clear();
        meta = HDFMeta{};

        std::ifstream fin(path, std::ios::binary);
        if (!fin) {
            err = "无法打开HDF文件";
            return false;
        }

        const size_t probeSize = 65536;
        std::vector<char> probe(probeSize, 0);
        fin.read(probe.data(), static_cast<std::streamsize>(probeSize));
        size_t got = static_cast<size_t>(fin.gcount());
        if (got == 0) {
            err = "HDF文件为空";
            return false;
        }

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

            if (key == "kind") meta.kind = val;
            else if (key == "byte order") meta.byteOrder = val;
            else if (key == "scan mode") meta.scanMode = val;
            else if (key == "ch order") meta.chOrderRaw = val;
            else if (key == "data org") meta.dataOrgRaw = val;
            else if (key == "nbr of channel") meta.nChannels = SafeParse<size_t>(val, 1);
            else if (key == "nbr of scans") meta.nScans = SafeParse<size_t>(val, 0);
            else if (key == "delta value") meta.deltaValue = SafeParse<double>(val, 0.0);
            else if (key == "first value") meta.firstValue = SafeParse<double>(val, 0.0);
            else if (inChannelDef && key == "name str") curCh.name = val.empty() ? "Channel" : val;
            else if (inChannelDef && key == "title str") curCh.label = val;
            else if (inChannelDef && key == "physical unit") curCh.unit = val.empty() ? "-" : val;
            else if (inChannelDef && key == "physical quantity") curCh.quantity = val;
            else if (inChannelDef && key == "implementation type") curCh.implementationType = val;
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
                meta.channels[i].implementationType = "FLOAT32";
            }
        }
        else if (meta.channels.size() > meta.nChannels) {
            meta.channels.resize(meta.nChannels);
        }

        // 扩展区 DOF
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
                        currentExtCh = SafeParse<int>(inner.substr(7), -1);
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

    // ========================= ch order解析 =========================
    // 规则：
    // - X*N => 第N通道为比例通道，coeff=X
    // - N   => 第N通道固定1000Hz
    static bool BuildChannelRateRules(const HDFMeta& meta, std::vector<ChannelRateRule>& rules) {
        rules.assign(meta.nChannels, ChannelRateRule{});

        if (meta.chOrderRaw.empty()) {
            for (size_t i = 0; i < meta.nChannels; ++i) {
                rules[i].coeff = 1;
            }
            return true;
        }

        std::string s = meta.chOrderRaw;
        for (char& c : s) {
            if (c == ',' || c == ';' || c == '\t') c = ' ';
        }

        std::stringstream ss(s);
        std::string tok;

        while (ss >> tok) {
            tok = Trim(tok);
            if (tok.empty()) continue;

            auto p = tok.find('*');
            if (p != std::string::npos) {
                size_t x = SafeParse<size_t>(tok.substr(0, p), 0);
                size_t n = SafeParse<size_t>(tok.substr(p + 1), 0);
                if (x == 0 || n == 0 || n > meta.nChannels) continue;
                rules[n - 1].fixed1000 = false;
                rules[n - 1].coeff = x;
            }
            else {
                size_t n = SafeParse<size_t>(tok, 0);
                if (n == 0 || n > meta.nChannels) continue;
                rules[n - 1].fixed1000 = true;
                rules[n - 1].coeff = 0;
            }
        }

        // 未覆盖的通道回退为比例1
        for (size_t i = 0; i < rules.size(); ++i) {
            if (!rules[i].fixed1000 && rules[i].coeff == 0) {
                rules[i].coeff = 1;
            }
        }

        return true;
    }

    // ========================= 采样率计算 =========================
    // simultaneous:
    //   Fi = Fbase
    // synchronised multiple:
    //   C1000 = 固定1000Hz通道个数
    //   S = 比例通道coeff之和（不包含1000Hz）
    //   Fremain = Fbase - C1000*1000
    //   K = Fremain / S
    //   比例通道: Fi = Xi*K
    //   固定通道: Fi = 1000
    static bool ComputePerChannelFs(const HDFMeta& meta,
        const std::vector<ChannelRateRule>& rules,
        std::vector<double>& outFs,
        double& outFbase,
        double& outK,
        size_t& outSumCoeff,
        size_t& outFixed1000Count,
        double& outFremain)
    {
        outFs.clear();
        outFbase = 0.0;
        outK = 0.0;
        outSumCoeff = 0;
        outFixed1000Count = 0;
        outFremain = 0.0;

        if (meta.deltaValue <= 0.0 || rules.empty()) return false;

        outFbase = 1.0 / meta.deltaValue;

        if (IsSimultaneous(meta)) {
            outFs.assign(rules.size(), outFbase);
            outK = outFbase;
            outSumCoeff = rules.size();
            outFixed1000Count = 0;
            outFremain = outFbase;
            return true;
        }

        if (IsSynchronisedMultiple(meta)) {
            size_t S = 0;
            size_t C1000 = 0;
            for (const auto& r : rules) {
                if (r.fixed1000) ++C1000;
                else S += r.coeff;
            }

            if (S == 0) return false;

            double Fremain = outFbase - static_cast<double>(C1000) * 1000.0;
            if (Fremain <= 0.0) return false;

            outSumCoeff = S;
            outFixed1000Count = C1000;
            outFremain = Fremain;
            outK = Fremain / static_cast<double>(S);

            outFs.resize(rules.size(), 0.0);
            for (size_t i = 0; i < rules.size(); ++i) {
                if (rules[i].fixed1000) outFs[i] = 1000.0;
                else outFs[i] = static_cast<double>(rules[i].coeff) * outK;
            }
            return true;
        }

        // 未知scan mode回退为同频
        outFs.assign(rules.size(), outFbase);
        outK = outFbase;
        outSumCoeff = rules.size();
        outFixed1000Count = 0;
        outFremain = outFbase;
        return true;
    }

    // ========================= 数据读取 =========================
    // simultaneous:
    //   每帧每通道1点
    // synchronised multiple:
    //   X*N 通道：每帧 Xi 点
    //   单独N通道：每帧 1 点
    static bool ExtractChannelData(const HDFMeta& meta,
        const std::vector<unsigned char>& raw,
        size_t targetCh,
        const std::vector<ChannelRateRule>& rules,
        std::vector<float>& outData)
    {
        outData.clear();
        if (targetCh >= meta.nChannels) return false;

        const bool littleFile = IsLittleEndianFile(meta);

        if (IsSimultaneous(meta)) {
            size_t bytesPerFrame = 0;
            for (size_t i = 0; i < meta.nChannels; ++i) {
                bytesPerFrame += BytesPerSampleFromImpl(meta.channels[i].implementationType);
            }
            if (bytesPerFrame == 0 || raw.size() < bytesPerFrame) return false;

            size_t nFrames = raw.size() / bytesPerFrame;
            outData.reserve(nFrames);

            for (size_t f = 0; f < nFrames; ++f) {
                const unsigned char* frameBase = raw.data() + f * bytesPerFrame;
                size_t off = 0;
                for (size_t ch = 0; ch < meta.nChannels; ++ch) {
                    size_t bps = BytesPerSampleFromImpl(meta.channels[ch].implementationType);
                    if (ch == targetCh) {
                        std::string impl = ToLowerCopy(meta.channels[ch].implementationType);
                        if (impl.find("uint32") != std::string::npos) {
                            outData.push_back(static_cast<float>(ReadU32(frameBase + off, littleFile)));
                        }
                        else {
                            outData.push_back(ReadF32(frameBase + off, littleFile));
                        }
                    }
                    off += bps;
                }
            }
            return !outData.empty();
        }

        if (IsSynchronisedMultiple(meta)) {
            auto sampleCountPerFrame = [&](size_t ch) -> size_t {
                return rules[ch].fixed1000 ? 1 : std::max<size_t>(rules[ch].coeff, 1);
                };

            size_t bytesPerFrame = 0;
            for (size_t i = 0; i < meta.nChannels; ++i) {
                bytesPerFrame += sampleCountPerFrame(i) * BytesPerSampleFromImpl(meta.channels[i].implementationType);
            }
            if (bytesPerFrame == 0 || raw.size() < bytesPerFrame) return false;

            size_t nFrames = raw.size() / bytesPerFrame;
            outData.reserve(nFrames * sampleCountPerFrame(targetCh));

            for (size_t f = 0; f < nFrames; ++f) {
                const unsigned char* frameBase = raw.data() + f * bytesPerFrame;
                size_t off = 0;

                for (size_t ch = 0; ch < meta.nChannels; ++ch) {
                    size_t bps = BytesPerSampleFromImpl(meta.channels[ch].implementationType);
                    size_t count = sampleCountPerFrame(ch);
                    std::string impl = ToLowerCopy(meta.channels[ch].implementationType);

                    for (size_t k = 0; k < count; ++k) {
                        if (ch == targetCh) {
                            const unsigned char* p = frameBase + off + k * bps;
                            if (impl.find("uint32") != std::string::npos) {
                                outData.push_back(static_cast<float>(ReadU32(p, littleFile)));
                            }
                            else {
                                outData.push_back(ReadF32(p, littleFile));
                            }
                        }
                    }
                    off += count * bps;
                }
            }
            return !outData.empty();
        }

        return false;
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

    std::vector<ChannelRateRule> rules;
    if (!BuildChannelRateRules(meta, rules)) {
        std::cerr << "[错误] ch order 解析失败: " << hdfPath << std::endl;
        return false;
    }

    std::vector<double> fsPerCh;
    double fbase = 0.0, K = 0.0, Fremain = 0.0;
    size_t S = 0, C1000 = 0;
    if (!ComputePerChannelFs(meta, rules, fsPerCh, fbase, K, S, C1000, Fremain)) {
        std::cerr << "[错误] 采样率计算失败: " << hdfPath << std::endl;
        return false;
    }

    sampleRate = fbase;

    outChannels.reserve(meta.nChannels);
    for (size_t i = 0; i < meta.nChannels; ++i) {
        HDFChannelInfo ch;
        ch.channelName = meta.channels[i].name.empty() ? ("Channel " + std::to_string(i + 1)) : meta.channels[i].name;
        ch.channelLabel = meta.channels[i].label.empty() ? ch.channelName : meta.channels[i].label;
        ch.dataType = meta.channels[i].implementationType;
        ch.dataLength = meta.nScans;
        ch.dataOffset = meta.startOfData;
        ch.unit = meta.channels[i].unit.empty() ? "-" : meta.channels[i].unit;
        ch.dof = meta.channels[i].dof.empty() ? "-" : meta.channels[i].dof;

        double fi = (i < fsPerCh.size() ? fsPerCh[i] : fbase);

        ch.sampleRate = fi;
        ch.sampleRateTrusted = (fi > 0.0 && std::isfinite(fi));

        ch.internalSampleRate = fi;
        ch.internalSampleRateTrusted = ch.sampleRateTrusted;
        ch.effectiveSampleRate = fi;
        ch.effectiveSampleRateTrusted = ch.sampleRateTrusted;

        outChannels.push_back(std::move(ch));
    }

    std::cout << "[DEBUG] HDF GetAllChannels:"
        << " path=" << hdfPath
        << ", scanMode=" << meta.scanMode
        << ", nChannels=" << meta.nChannels
        << ", nScans=" << meta.nScans
        << ", delta=" << meta.deltaValue
        << ", Fbase=" << fbase
        << ", fixed1000Count=" << C1000
        << ", Fremain=" << Fremain
        << ", SumCoeff=" << S
        << ", K=" << K
        << ", chOrderRaw=" << meta.chOrderRaw
        << ", dataOrg=" << meta.dataOrgRaw
        << std::endl;

    for (size_t i = 0; i < outChannels.size(); ++i) {
        std::cout << "  [DEBUG] ch" << (i + 1)
            << " name=" << outChannels[i].channelName
            << " unit=" << outChannels[i].unit
            << " dof=" << outChannels[i].dof
            << " Fs=" << outChannels[i].sampleRate
            << (rules[i].fixed1000 ? " [fixed1000]" : " [ratio]")
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

    if (!IsTimeKind(meta) || meta.nChannels == 0 || meta.deltaValue <= 0.0) {
        std::cerr << "[错误] HDF元数据无效: " << hdfPath << std::endl;
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

    std::vector<ChannelRateRule> rules;
    if (!BuildChannelRateRules(meta, rules)) {
        std::cerr << "[错误] ch order 解析失败: " << hdfPath << std::endl;
        return false;
    }

    std::vector<double> fsPerCh;
    double fbase = 0.0, K = 0.0, Fremain = 0.0;
    size_t S = 0, C1000 = 0;
    if (!ComputePerChannelFs(meta, rules, fsPerCh, fbase, K, S, C1000, Fremain)) {
        std::cerr << "[错误] 采样率计算失败: " << hdfPath << std::endl;
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

    if (!ExtractChannelData(meta, raw, targetCh, rules, outData) || outData.empty()) {
        std::cerr << "[错误] 通道数据解析失败: " << channelName << std::endl;
        return false;
    }

    sampleRate = (targetCh < fsPerCh.size() ? fsPerCh[targetCh] : fbase);
    sampleRateTrusted = (sampleRate > 0.0 && std::isfinite(sampleRate));

    std::cout << "[DEBUG] HDF ReadChannelDataWithFsQuality:"
        << " channel=" << channelName
        << ", outData.size=" << outData.size()
        << ", Fs=" << sampleRate
        << ", trusted=" << sampleRateTrusted
        << ", Fbase=" << fbase
        << ", fixed1000Count=" << C1000
        << ", Fremain=" << Fremain
        << ", SumCoeff=" << S
        << ", K=" << K
        << ", scanMode=" << meta.scanMode
        << ", chOrderRaw=" << meta.chOrderRaw
        << std::endl;

    return true;
}