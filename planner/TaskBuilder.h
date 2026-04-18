#pragma once
#include <string>
#include <vector>
#include "../domain/Types.h"

// Qt/上层可直接填写的输入结构
struct BuildRequest {
    // 输入文件路径（支持 wav/atfx/hdf/h5）
    std::vector<std::string> inputPaths;

    // 分析模式
    AnalysisMode mode = AnalysisMode::FFT;

    // FFT参数（先做统一参数模式，后续可扩展按文件/按通道）
    FFTParams fftParams{};

    // 是否默认选中全部文件
    bool selectAllFiles = true;

    // ATFX: 每个文件选择的“分析通道索引（0-based）”
    // 若某个atfx文件未给出，则默认全选通道
    std::vector<std::vector<size_t>> atfxSelectedChannelsByFile;

    // FFT vs rpm 相关（ATFX/HDF有效）
    // 每个文件对应一个rpm通道名；长度不足时按空处理（HDF可尝试自动识别）
    std::vector<std::string> rpmChannelNameByFile;
    double rpmBinStep = 50.0;
};

struct BuildResponse {
    std::vector<FileItem> files;
    std::vector<Job> jobs;
    bool ok = false;
    std::string message;
};

class TaskBuilder {
public:
    // 纯参数构建，不做交互
    static BuildResponse BuildFromRequest(const BuildRequest& req);
};
