#pragma once
#include "../domain/Types.h"

class LevelVsRpmFlow {
public:
    static JobResult Run(const Job& job, const FileItem& file);
};