#include "RunRequestMapper.h"

MappedRunConfig MapRunRequest(const RunRequest& in) {
    MappedRunConfig out{};

    // BuildRequest
    out.buildReq.inputPaths = in.inputPaths;
    out.buildReq.mode = in.mode;
    out.buildReq.fftParams = in.fftParams;
    out.buildReq.selectAllFiles = true;
    out.buildReq.atfxSelectedChannelsByFile = in.atfxSelectedChannelsByFile;
    out.buildReq.rpmChannelNameByFile = in.rpmChannelNameByFile;
    out.buildReq.rpmBinStep = in.rpmBinStep;

    // EngineRunConfig
    out.engineCfg.maxThreads = in.maxThreads;
    out.engineCfg.maxRetries = in.maxRetries;
    out.engineCfg.enableCancel = in.enableCancel;

    return out;
}