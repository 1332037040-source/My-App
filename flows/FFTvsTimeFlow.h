#pragma once
#include "../domain/Types.h"

class FFTvsTimeFlow {
public:
    JobResult Run(const Job& job, const FileItem& file);
};