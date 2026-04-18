#include "ParseUtils.h"
#include <algorithm>
#include <cctype>
#include <iostream>
#include <fstream>
#include <sstream>
#include <set>
#include <limits>
#include <cmath>

std::string get_ext_lower(const std::string& fn) {
    auto pos = fn.find_last_of(".");
    if (pos == std::string::npos) return "";
    std::string ext = fn.substr(pos + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return ext;
}

std::string trim_copy(const std::string& s) {
    auto b = s.begin();
    while (b != s.end() && std::isspace((unsigned char)*b)) ++b;
    auto e = s.end();
    while (e != b && std::isspace((unsigned char)*(e - 1))) --e;
    return std::string(b, e);
}

std::vector<size_t> parse_indices_1based(const std::string& input, size_t maxCount) {
    std::vector<size_t> out;
    if (maxCount == 0) return out;

    std::string s = trim_copy(input);
    std::string low = s;
    std::transform(low.begin(), low.end(), low.begin(), ::tolower);

    if (low.empty() || low == "all" || low == "a") {
        for (size_t i = 0; i < maxCount; ++i) out.push_back(i);
        return out;
    }

    for (char& c : s) {
        if (c == ',' || c == ';' || c == '\t' || c == '|' || c == '/' || c == '\\') c = ' ';
    }

    std::stringstream ss(s);
    std::set<size_t> uniq;
    long long x = 0;
    while (ss >> x) {
        if (x >= 1 && static_cast<size_t>(x) <= maxCount) {
            uniq.insert(static_cast<size_t>(x) - 1);
        }
    }

    out.assign(uniq.begin(), uniq.end());
    return out;
}

bool ask_reuse_default_yes(const std::string& msg) {
    std::cout << msg << " (y/n, y): ";
    std::string yn;
    std::getline(std::cin, yn);
    yn = trim_copy(yn);
    std::transform(yn.begin(), yn.end(), yn.begin(), ::tolower);
    return yn.empty() || yn == "y" || yn == "yes" || yn == "1";
}

static Window::WindowType ParseWindowTypeFromInt(int win, Window::WindowType defVal) {
    switch (win) {
    case 1: return Window::WindowType::Rectangular;
    case 2: return Window::WindowType::Hanning;
    case 3: return Window::WindowType::Hamming;
    case 4: return Window::WindowType::Blackman;
    case 5: return Window::WindowType::Bartlett;
    case 6: return Window::WindowType::FlatTop;
    default: return defVal;
    }
}

static int WindowTypeToMenuIndex(Window::WindowType t) {
    switch (t) {
    case Window::WindowType::Rectangular: return 1;
    case Window::WindowType::Hanning:     return 2;
    case Window::WindowType::Hamming:     return 3;
    case Window::WindowType::Blackman:    return 4;
    case Window::WindowType::Bartlett:    return 5;
    case Window::WindowType::FlatTop:     return 6;
    default:                              return 2; // fallback
    }
}

static FFTParams AskFFTParamsImpl(const std::string* title, const FFTParams& d) {
    FFTParams p = d;

    if (title && !title->empty()) {
        std::cout << "\n========================================\n";
        std::cout << *title << "\n";
        std::cout << "========================================\n";
    }

    // 关键修正：支持默认值回车沿用，避免 TaskPlanner 里复用参数时被强制覆盖
    std::cout << "[1/5] 窗函数 (1-Rect,2-Hanning,3-Hamming,4-Blackman,5-Bartlett,6-FlatTop)"
        << " [默认 " << WindowTypeToMenuIndex(d.window_type) << "]: ";
    std::string winStr;
    std::getline(std::cin, winStr);
    winStr = trim_copy(winStr);
    if (!winStr.empty()) {
        try {
            int win = std::stoi(winStr);
            p.window_type = ParseWindowTypeFromInt(win, d.window_type);
        }
        catch (...) {
            p.window_type = d.window_type;
        }
    }
    else {
        p.window_type = d.window_type;
    }

    std::cout << "[2/5] FFT块长(默认 " << d.block_size << "): ";
    std::string bsStr;
    std::getline(std::cin, bsStr);
    bsStr = trim_copy(bsStr);
    if (!bsStr.empty()) {
        try {
            long long bs = std::stoll(bsStr);
            p.block_size = (bs > 0) ? static_cast<size_t>(bs) : d.block_size;
        }
        catch (...) {
            p.block_size = d.block_size;
        }
    }
    else {
        p.block_size = d.block_size;
    }

    std::cout << "[3/5] 重叠比例（0-1，默认 " << d.overlap_ratio << "): ";
    std::string ovStr;
    std::getline(std::cin, ovStr);
    ovStr = trim_copy(ovStr);
    if (!ovStr.empty()) {
        try {
            double ov = std::stod(ovStr);
            p.overlap_ratio = (ov >= 0.0 && ov < 1.0) ? ov : d.overlap_ratio;
        }
        catch (...) {
            p.overlap_ratio = d.overlap_ratio;
        }
    }
    else {
        p.overlap_ratio = d.overlap_ratio;
    }

    std::cout << "[4/5] 幅值计权(1-Peak,2-RMS) [默认 "
        << ((d.amp_scaling == Analyzer::AmplitudeScaling::RMS) ? 2 : 1) << "]: ";
    std::string scStr;
    std::getline(std::cin, scStr);
    scStr = trim_copy(scStr);
    if (!scStr.empty()) {
        try {
            int sc = std::stoi(scStr);
            p.amp_scaling = (sc == 2) ? Analyzer::AmplitudeScaling::RMS : Analyzer::AmplitudeScaling::Peak;
        }
        catch (...) {
            p.amp_scaling = d.amp_scaling;
        }
    }
    else {
        p.amp_scaling = d.amp_scaling;
    }

    std::cout << "[5/5] 频率计权(0-无,1-A,2-B,3-C,4-D) [默认 " << static_cast<int>(d.weight_type) << "]: ";
    std::string wtStr;
    std::getline(std::cin, wtStr);
    wtStr = trim_copy(wtStr);
    if (!wtStr.empty()) {
        try {
            int wt = std::stoi(wtStr);
            if (wt < 0) wt = 0;
            if (wt > 4) wt = 4;
            p.weight_type = static_cast<Weighting::WeightType>(wt);
        }
        catch (...) {
            p.weight_type = d.weight_type;
        }
    }
    else {
        p.weight_type = d.weight_type;
    }

    return p;
}

FFTParams AskFFTParams(const FFTParams& d) {
    return AskFFTParamsImpl(nullptr, d);
}

FFTParams AskFFTParamsWithTitle(const std::string& title, const FFTParams& d) {
    return AskFFTParamsImpl(&title, d);
}

bool LoadSpectrumFromCsv(const std::string& csvPath, std::vector<double>& outFreq, std::vector<double>& outMag) {
    outFreq.clear();
    outMag.clear();

    std::ifstream fin(csvPath);
    if (!fin) return false;

    std::string line;
    bool first = true;
    while (std::getline(fin, line)) {
        if (line.empty()) continue;
        if (first) {
            first = false;
            if (line.find("Frequency") != std::string::npos) continue;
        }

        std::stringstream ss(line);
        std::string c1, c2;
        if (!std::getline(ss, c1, ',')) continue;
        if (!std::getline(ss, c2, ',')) continue;

        try {
            outFreq.push_back(std::stod(c1));
            outMag.push_back(std::stod(c2));
        }
        catch (...) {
            // 忽略转换错误
        }
    }

    return !outFreq.empty() && outFreq.size() == outMag.size();
}

PeakPreview CalcPeakFromCsvData(const std::vector<double>& freq, const std::vector<double>& mag) {
    PeakPreview p;
    if (freq.empty() || mag.empty() || freq.size() != mag.size()) return p;

    size_t begin = (mag.size() > 1) ? 1 : 0;
    size_t maxI = begin;
    double maxV = -1.0;
    for (size_t i = begin; i < mag.size(); ++i) {
        if (mag[i] > maxV) {
            maxV = mag[i];
            maxI = i;
        }
    }

    p.idx = maxI;
    p.freq = freq[maxI];
    p.mag = mag[maxI];

    if (maxI > 0 && maxI + 1 < mag.size()) {
        double yL = mag[maxI - 1], yC = mag[maxI], yR = mag[maxI + 1];
        double yInterp = 0.5 * (yL + yR);
        p.errAbs = std::abs(yC - yInterp);
        p.errPct = (std::abs(yC) > 1e-12) ? (p.errAbs / std::abs(yC) * 100.0) : 0.0;
    }
    return p;
}