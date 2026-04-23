#pragma once

#include "CoreAnalysisRequest.h"
#include "CoreAnalysisResult.h"

class CoreAnalysisFacade
{
public:
    CoreAnalysisFacade() = default;
    ~CoreAnalysisFacade() = default;

    // VS/CLI：写CSV（可同时返回内存数组，取决于request.returnInMemory）
    CoreAnalysisResult runForCliCsv(const CoreAnalysisRequest& request);

    // Qt：只返回内存数组，不写CSV，不返回CSV路径
    CoreAnalysisResult runForQtMemory(const CoreAnalysisRequest& request);

private:
    bool validateRequest(const CoreAnalysisRequest& request, CoreAnalysisResult& result);

    CoreAnalysisResult runImpl(
        const CoreAnalysisRequest& request,
        bool forceWriteCsv,
        bool returnFilePaths,
        bool forceReturnInMemory);
};