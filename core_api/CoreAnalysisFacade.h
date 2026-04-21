#pragma once

#include <string>
#include <vector>
#include "CoreAnalysisRequest.h"
#include "CoreAnalysisResult.h"

class CoreAnalysisFacade
{
public:
    CoreAnalysisFacade() = default;
    ~CoreAnalysisFacade() = default;

    CoreAnalysisResult run(const CoreAnalysisRequest& request);

private:
    bool validateRequest(const CoreAnalysisRequest& request, CoreAnalysisResult& result);
};