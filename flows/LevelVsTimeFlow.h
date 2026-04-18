#pragma once

#include "../domain/Types.h"

class LevelVsTimeFlow {
public:
    static JobResult Run(const Job& job, const FileItem& file);
};