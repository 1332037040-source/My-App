#pragma once
#include <vector>
#include <map>
#include "../domain/Types.h"

class TaskPlanner {
public:
    bool CollectInputPaths(std::vector<std::string>& inputPaths);
    bool BuildFileItems(const std::vector<std::string>& inputPaths, std::vector<FileItem>& files);
    bool SelectFiles(std::vector<FileItem>& files);
    bool LoadAndSelectChannels(std::vector<FileItem>& files);

    // 输出完整任务列表
    bool ConfigureParamsAndBuildJobs(std::vector<FileItem>& files, std::vector<Job>& jobs);
};