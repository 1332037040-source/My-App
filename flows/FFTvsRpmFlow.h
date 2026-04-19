#pragma once
#include "../domain/Types.h"

class FFTvsRpmFlow {
public:
    JobResult Run(const Job& job, const FileItem& file);
};