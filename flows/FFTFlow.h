#pragma once
#include "../domain/Types.h"

class FFTFlow {
public:
    JobResult Run(const Job& job, const FileItem& file);
};