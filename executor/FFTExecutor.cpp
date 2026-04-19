#include "FFTExecutor.h"

#include "flows/FFTFlow.h"
#include "flows/FFTvsTimeFlow.h"
#include "flows/FFTvsRpmFlow.h"
#include "flows/OctaveFlow.h"
#include "flows/LevelVsTimeFlow.h"

JobResult FFTExecutor::RunOne(const Job& job, const FileItem& file)
{
    // 1) 常规 FFT
    if (job.mode == AnalysisMode::FFT) {
        FFTFlow flow;
        return flow.Run(job, file);
    }

    // 2) FFT vs Time
    if (job.mode == AnalysisMode::FFT_VS_TIME) {
        FFTvsTimeFlow flow;
        return flow.Run(job, file);
    }

    // 3) FFT vs RPM
    if (job.mode == AnalysisMode::FFT_VS_RPM) {
        FFTvsRpmFlow flow;
        return flow.Run(job, file);
    }

    // 4) Level vs Time
    if (job.mode == AnalysisMode::LEVEL_VS_TIME) {
        LevelVsTimeFlow flow;
        return flow.Run(job, file);
    }

    // 5) Octave
    if (job.mode == AnalysisMode::OCTAVE_1_1 || job.mode == AnalysisMode::OCTAVE_1_3) {
        OctaveFlow flow;
        return flow.Run(job, file);
    }

    JobResult r;
    r.ok = false;
    r.message = "不支持的分析模式";
    return r;
}