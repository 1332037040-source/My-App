#include "ParallelRunner.h"
#include "FFTExecutor.h"

#include <thread>
#include <mutex>
#include <atomic>
#include <algorithm>
#include <iostream>
#include <chrono>
#include <string>
#include <sstream>
#include <iomanip>

#ifdef _WIN32
#include <conio.h>
#endif

static bool is_retryable_error(const std::string& msg) {
    std::string s = msg;
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    return (s.find("读取") != std::string::npos) ||
        (s.find("写入") != std::string::npos) ||
        (s.find("io") != std::string::npos) ||
        (s.find("busy") != std::string::npos) ||
        (s.find("lock") != std::string::npos);
}

static std::string make_progress_text(size_t done, size_t total, size_t ok, size_t fail, bool cancelled) {
    const int barWidth = 30;
    double ratio = (total == 0) ? 1.0 : (double)done / (double)total;
    int filled = (int)(ratio * barWidth);
    if (filled < 0) filled = 0;
    if (filled > barWidth) filled = barWidth;

    std::string bar(barWidth, '-');
    for (int i = 0; i < filled; ++i) bar[i] = '#';

    std::ostringstream oss;
    oss << "[进度] [" << bar << "] "
        << done << "/" << total
        << " | OK=" << ok
        << " FAIL=" << fail;
    if (cancelled) oss << " | 已请求取消(Q)";
    return oss.str();
}

std::vector<JobResult> ParallelRunner::RunAll(
    FFTExecutor& executor,
    const std::vector<Job>& jobs,
    const std::vector<FileItem>& files,
    const Options& options,
    ProgressCallback onProgress,
    LogCallback onLog,
    CancelCallback shouldCancel
) {
    std::vector<JobResult> results(jobs.size());
    if (jobs.empty()) return results;

    auto emitLog = [&](const std::string& s) {
        if (onLog) onLog(s);
        else std::cout << s << "\n";
        };

    auto emitProgress = [&](size_t done, size_t total, size_t ok, size_t fail, const std::string& msg) {
        if (onProgress) onProgress(done, total, ok, fail, msg);
        else std::cout << msg << "\n" << std::flush;
        };

    size_t hw = std::thread::hardware_concurrency();
    if (hw == 0) hw = 4;

    size_t autoThreads = (hw <= 2) ? 1 : (hw - 1);
    size_t threadCount = (options.maxThreads == 0) ? autoThreads : options.maxThreads;
    threadCount = std::max<size_t>(1, std::min(threadCount, jobs.size()));

    const size_t chunkSize = 3;

    {
        std::ostringstream oss;
        oss << "[并行优化] 任务数=" << jobs.size()
            << ", 线程数=" << threadCount
            << ", chunk=" << chunkSize
            << ", 重试=" << options.maxRetries
            << ", 保留时域CSV=是";
        emitLog(oss.str());
    }

    std::atomic<size_t> nextIndex{ 0 };
    std::atomic<size_t> doneCount{ 0 };
    std::atomic<size_t> okCount{ 0 };
    std::atomic<size_t> failCount{ 0 };
    std::atomic<bool> cancelRequested{ false };
    std::atomic<bool> allDone{ false };

    auto is_cancelled = [&]() -> bool {
        if (cancelRequested.load()) return true;
        if (shouldCancel && shouldCancel()) {
            cancelRequested.store(true);
            return true;
        }
        return false;
        };

    auto run_one_with_retry = [&](size_t i) {
        const Job& job = jobs[i];
        const FileItem& file = files[job.fileIdx];

        JobResult finalResult;
        bool success = false;

        for (int attempt = 0; attempt <= options.maxRetries; ++attempt) {
            if (is_cancelled()) break;

            JobResult r = executor.RunOne(job, file);
            if (r.ok) {
                finalResult = std::move(r);
                success = true;
                break;
            }

            finalResult = std::move(r);

            if (attempt < options.maxRetries && is_retryable_error(finalResult.message)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(80 + 40 * attempt));
                continue;
            }
            else {
                break;
            }
        }

        if (!success && is_cancelled()) {
            finalResult.ok = false;
            finalResult.message = "任务被取消";
        }

        results[i] = finalResult;

        size_t d = doneCount.fetch_add(1) + 1;
        size_t o = okCount.load();
        size_t f = failCount.load();

        if (finalResult.ok) o = okCount.fetch_add(1) + 1;
        else f = failCount.fetch_add(1) + 1;

        emitProgress(d, jobs.size(), o, f, make_progress_text(d, jobs.size(), o, f, cancelRequested.load()));
        };

    auto worker = [&]() {
        while (true) {
            if (is_cancelled()) break;

            size_t begin = nextIndex.fetch_add(chunkSize);
            if (begin >= jobs.size()) break;

            size_t end = std::min(begin + chunkSize, jobs.size());
            for (size_t i = begin; i < end; ++i) {
                if (is_cancelled()) break;
                run_one_with_retry(i);
            }
        }
        };

    std::thread cancelThread;
#ifdef _WIN32
    if (options.enableCancelKey) {
        cancelThread = std::thread([&]() {
            while (!cancelRequested.load() && !allDone.load()) {
                if (_kbhit()) {
                    int ch = _getch();
                    if (ch == 'q' || ch == 'Q') {
                        cancelRequested.store(true);
                        break;
                    }
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(40));
            }
            });
    }
#endif

    std::vector<std::thread> workers;
    workers.reserve(threadCount);
    for (size_t t = 0; t < threadCount; ++t) workers.emplace_back(worker);
    for (auto& th : workers) if (th.joinable()) th.join();

    allDone.store(true);

#ifdef _WIN32
    if (cancelThread.joinable()) cancelThread.join();
#endif

    emitProgress(doneCount.load(), jobs.size(), okCount.load(), failCount.load(),
        make_progress_text(doneCount.load(), jobs.size(), okCount.load(), failCount.load(), cancelRequested.load()));

    if (cancelRequested.load()) emitLog("[并行] 已取消，未开始任务未执行。");
    else emitLog("[并行] 全部任务处理完成。");

    return results;
}