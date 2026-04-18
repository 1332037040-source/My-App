#pragma once
#include <functional>
#include <string>
#include <vector>

#include "../domain/Types.h"
#include "../executor/ParallelRunner.h"

struct EngineRunConfig {
    std::vector<FileItem> files;
    std::vector<Job> jobs;

    size_t maxThreads = 0;   // 0 = auto
    int maxRetries = 1;
    bool enableCancel = false; // CLI可开Q取消，Qt通常走shouldCancel回调
};

struct EngineProgress {
    size_t done = 0;
    size_t total = 0;
    size_t ok = 0;
    size_t fail = 0;
    std::string message;
};

struct EngineRunResult {
    std::vector<JobResult> results;
    int success = 0;
    int failed = 0;
    bool cancelled = false;
};

class FFT11Engine {
public:
    using ProgressCallback = std::function<void(const EngineProgress&)>;
    using LogCallback = std::function<void(const std::string&)>;
    using CancelCallback = std::function<bool()>; // true=请求取消

    EngineRunResult Run(
        const EngineRunConfig& cfg,
        ProgressCallback onProgress = nullptr,
        LogCallback onLog = nullptr,
        CancelCallback shouldCancel = nullptr
    );
};