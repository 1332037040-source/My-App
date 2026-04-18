#pragma once
#include "../domain/Types.h"

class OctaveFlow {
public:
    JobResult Run(const Job& job, const FileItem& file);
};