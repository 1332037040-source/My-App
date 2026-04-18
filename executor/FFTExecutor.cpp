#include "FFTExecutor.h"

#include "flows/FFTFlow.h"
#include "flows/FFTvsTimeFlow.h"
#include "flows/OctaveFlow.h"
#include "flows/LevelVsTimeFlow.h"

JobResult FFTExecutor::RunOne(const Job& job, const FileItem& file)
{
    // 1) 常规 FFT -> FFTFlow
    if (job.mode == AnalysisMode::FFT) {
        FFTFlow flow;
        return flow.Run(job, file);
    }

    // 2) FFT vs Time -> FFTvsTimeFlow
    if (job.mode == AnalysisMode::FFT_VS_TIME) {
        FFTvsTimeFlow flow;
        return flow.Run(job, file);
    }

    // 3) Level vs Time -> LevelVsTimeFlow
    if (job.mode == AnalysisMode::LEVEL_VS_TIME) {
        LevelVsTimeFlow flow;
        return flow.Run(job, file);
    }

    // 4) Octave -> OctaveFlow
    if (job.mode == AnalysisMode::OCTAVE_1_1 || job.mode == AnalysisMode::OCTAVE_1_3) {
        OctaveFlow flow;
        return flow.Run(job, file);
    }

    // 5) 暂未实现：FFT_VS_RPM
    JobResult r;
    r.ok = false;

    if (job.mode == AnalysisMode::FFT_VS_RPM) {
        if (job.isATFX) {
            r.message = "FFT vs rpm缺少rpm通道或不支持在频域octave模式";
        }
        else if (file.ext == "hdf") {
            r.message = "FFT vs rpm当前不支持HDF";
        }
        else {
            r.message = "FFT vs rpm当前不支持WAV";
        }
        return r;
    }

    r.message = "不支持的分析模式";
    return r;
}