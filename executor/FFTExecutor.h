#pragma once

#include "../domain/Types.h"

class FFTExecutor {
public:
    JobResult RunOne(const Job& job, const FileItem& file);
};