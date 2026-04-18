#pragma once
#include "../domain/Types.h"
#include <vector>
#include <string>
#include <functional>

class FFTExecutor;

class ParallelRunner {
public:
    struct Options {
        size_t maxThreads = 0;   // 0=auto
        int maxRetries = 1;
        bool enableCancelKey = true; // 仅CLI下有意义
    };

    using ProgressCallback = std::function<void(
        size_t done, size_t total, size_t ok, size_t fail, const std::string& message)>;

    using LogCallback = std::function<void(const std::string&)>;
    using CancelCallback = std::function<bool()>; // true=请求取消

    static std::vector<JobResult> RunAll(
        FFTExecutor& executor,
        const std::vector<Job>& jobs,
        const std::vector<FileItem>& files,
        const Options& options,
        ProgressCallback onProgress = nullptr,
        LogCallback onLog = nullptr,
        CancelCallback shouldCancel = nullptr
    );
};