#include "Engine.h"
#include "../executor/FFTExecutor.h"

EngineRunResult FFT11Engine::Run(
    const EngineRunConfig& cfg,
    ProgressCallback onProgress,
    LogCallback onLog,
    CancelCallback shouldCancel
) {
    EngineRunResult out{};

    FFTExecutor executor;

    ParallelRunner::Options opt{};
    opt.maxThreads = cfg.maxThreads;
    opt.maxRetries = cfg.maxRetries;
    opt.enableCancelKey = cfg.enableCancel; // CLI场景，Qt通过false

    auto progressBridge = [&](size_t done, size_t total, size_t ok, size_t fail, const std::string& msg) {
        if (onProgress) {
            EngineProgress p;
            p.done = done;
            p.total = total;
            p.ok = ok;
            p.fail = fail;
            p.message = msg;
            onProgress(p);
        }
        };

    auto logBridge = [&](const std::string& msg) {
        if (onLog) onLog(msg);
        };

    out.results = ParallelRunner::RunAll(
        executor, cfg.jobs, cfg.files, opt,
        progressBridge,
        logBridge,
        shouldCancel
    );

    for (const auto& r : out.results) {
        if (r.ok) out.success++;
        else out.failed++;
    }

    out.cancelled = (shouldCancel ? shouldCancel() : false);
    return out;
}
