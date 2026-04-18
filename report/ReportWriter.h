#pragma once
#include <vector>
#include "../domain/Types.h"

class ReportWriter {
public:
    void PrintPlan(const std::vector<Job>& jobs, const std::vector<FileItem>& files);
    void PrintProgress(size_t idx, size_t total, const Job& job, const FileItem& file);
    void PrintResult(const JobResult& r);
    void PrintSummary(int success, int total);
};