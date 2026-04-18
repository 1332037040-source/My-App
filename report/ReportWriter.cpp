#include "ReportWriter.h"
#include <iostream>
#include <iomanip>
#include <cmath>

namespace {
    bool IsHdfExt(const std::string& ext) {
        return ext == "hdf" || ext == "h5" || ext == "hdf5";
    }
}

void ReportWriter::PrintPlan(const std::vector<Job>& jobs, const std::vector<FileItem>& files) {
    std::cout << "\n================ 执行计划 ================\n";
    for (size_t i = 0; i < jobs.size(); ++i) {
        const auto& j = jobs[i];
        const auto& f = files[j.fileIdx];

        if (j.isATFX) {
            const auto& ch = f.channels[j.channelIdx];
            std::cout << "[" << (i + 1) << "] ATFX | "
                << "文件[" << (j.fileIdx + 1) << "] " << f.path
                << " | 通道[" << (j.channelIdx + 1) << "] " << ch.channelName
                << " | N=" << j.params.block_size
                << " | overlap=" << j.params.overlap_ratio
                << "\n";
        }
        else if (IsHdfExt(f.ext)) {
            const auto& ch = f.channels[j.channelIdx];
            std::cout << "[" << (i + 1) << "] HDF  | "
                << "文件[" << (j.fileIdx + 1) << "] " << f.path
                << " | 通道[" << (j.channelIdx + 1) << "] " << ch.channelName
                << " | N=" << j.params.block_size
                << " | overlap=" << j.params.overlap_ratio
                << "\n";
        }
        else {
            std::cout << "[" << (i + 1) << "] WAV  | "
                << "文件[" << (j.fileIdx + 1) << "] " << f.path
                << " | N=" << j.params.block_size
                << " | overlap=" << j.params.overlap_ratio
                << "\n";
        }
    }
    std::cout << "==========================================\n";
    std::cout << "[计划] 将依次执行 " << jobs.size() << " 个FFT任务\n";
}

void ReportWriter::PrintProgress(size_t idx, size_t total, const Job& job, const FileItem& file) {
    std::cout << "\n============================================\n";
    std::cout << "[处理 " << idx << "/" << total << "] ";
    if (job.isATFX) {
        std::cout << "ATFX | 文件: " << file.path
            << " | 通道: " << file.channels[job.channelIdx].channelName
            << "\n";
    }
    else if (IsHdfExt(file.ext)) {
        std::cout << "HDF  | 文件: " << file.path
            << " | 通道: " << file.channels[job.channelIdx].channelName
            << "\n";
    }
    else {
        std::cout << "WAV  | 文件: " << file.path << "\n";
    }
}

void ReportWriter::PrintResult(const JobResult& r) {
    if (!r.ok) {
        std::cout << "[失败] " << r.message << "\n";
        return;
    }

    if (!r.timeCsvPath.empty()) {
        std::cout << "[时域] 时域CSV: " << r.timeCsvPath << "\n";
    }
    if (!r.fftCsvPath.empty()) {
        std::cout << "[频域] FFT CSV: " << r.fftCsvPath << "\n";
    }

    // 检查幅值是否有效（大于阈值）
    if (std::abs(r.peak.mag) > 1e-12) {
        std::cout << std::fixed << std::setprecision(3);
        std::cout << "[峰值] 峰值频率: " << r.peak.freq << " Hz\n";
        std::cout << "[峰值] 峰值幅值: " << r.peak.mag << "\n";
        std::cout << "[峰值] 峰值误差: " << r.peak.errAbs << " (" << r.peak.errPct << "%)\n";
    }
    else {
        std::cout << "[峰值] 频率: N/A\n";
        std::cout << "[峰值] 幅值: N/A\n";
        std::cout << "[峰值] 误差: N/A\n";
    }
}

void ReportWriter::PrintSummary(int success, int total) {
    std::cout << "\n============================================\n";
    std::cout << "处理完成！成功完成: " << success << " / " << total << " 个FFT任务\n";
}
