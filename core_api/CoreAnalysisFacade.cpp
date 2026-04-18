#include "CoreAnalysisFacade.h"

#include "../app/RunRequest.h"
#include "../app/RunRequestMapper.h"
#include "../preprocess/Weighting.h"
#include "../planner/TaskBuilder.h"
#include "../engine/Engine.h"
#include "../io/ATFXReader.h"
#include "../io/HDFReader.h"
#include "../domain/ParseUtils.h"

namespace
{
    AnalysisMode toAnalysisMode(const std::string& mode)
    {
        if (mode == "fft") {
            return AnalysisMode::FFT;
        }
        if (mode == "fft_vs_time") {
            return AnalysisMode::FFT_VS_TIME;
        }
        if (mode == "fft_vs_rpm") {
            return AnalysisMode::FFT_VS_RPM;
        }
        if (mode == "octave" || mode == "octave_1_1") {
            return AnalysisMode::OCTAVE_1_1;
        }
        if (mode == "octave_1_3") {
            return AnalysisMode::OCTAVE_1_3;
        }
        if (mode == "level_vs_time") {
            return AnalysisMode::LEVEL_VS_TIME;
        }

        return AnalysisMode::FFT;
    }

    bool tryResolveSelectedChannel(
        const std::string& filePath,
        const std::string& channelName,
        std::vector<size_t>& selectedChannels,
        std::string& errorMessage)
    {
        selectedChannels.clear();
        errorMessage.clear();

        if (channelName.empty()) {
            return true;
        }

        const std::string ext = get_ext_lower(filePath);

        if (ext == "wav") {
            return true;
        }

        if (ext == "atfx") {
            FFT11_ATFXReader reader;
            std::vector<ATFXChannelInfo> channels;
            double fs = 0.0;

            if (!reader.GetAllChannels(filePath, channels, fs) || channels.empty()) {
                errorMessage = "failed to read ATFX channels: " + filePath;
                return false;
            }

            for (size_t i = 0; i < channels.size(); ++i) {
                if (channels[i].channelName == channelName) {
                    selectedChannels.push_back(i);
                    return true;
                }
            }

            errorMessage = "channel not found in ATFX file: " + channelName;
            return false;
        }

        if (ext == "hdf") {
            FFT11_HDFReader reader;
            std::vector<HDFChannelInfo> channels;
            double fs = 0.0;

            if (!reader.GetAllChannels(filePath, channels, fs) || channels.empty()) {
                errorMessage = "failed to read HDF channels: " + filePath;
                return false;
            }

            for (size_t i = 0; i < channels.size(); ++i) {
                if (channels[i].channelName == channelName) {
                    selectedChannels.push_back(i);
                    return true;
                }
            }

            errorMessage = "channel not found in HDF file: " + channelName;
            return false;
        }

        errorMessage = "unsupported file type for channel selection: " + filePath;
        return false;
    }
}

bool CoreAnalysisFacade::validateRequest(const CoreAnalysisRequest& request, CoreAnalysisResult& result)
{
    if (request.filePath.empty()) {
        result.success = false;
        result.message = "filePath is empty.";
        return false;
    }

    if (request.analysisMode.empty()) {
        result.success = false;
        result.message = "analysisMode is empty.";
        return false;
    }

    if (request.outputDir.empty()) {
        result.success = false;
        result.message = "outputDir is empty.";
        return false;
    }

    if (request.fftSize == 0) {
        result.success = false;
        result.message = "fftSize must be greater than 0.";
        return false;
    }

    if (request.overlap < 0.0 || request.overlap >= 1.0) {
        result.success = false;
        result.message = "overlap must be in [0, 1).";
        return false;
    }

    const std::string& mode = request.analysisMode;
    if (mode != "fft" &&
        mode != "fft_vs_time" &&
        mode != "fft_vs_rpm" &&
        mode != "octave" &&
        mode != "octave_1_1" &&
        mode != "octave_1_3" &&
        mode != "level_vs_time") {
        result.success = false;
        result.message = "unsupported analysisMode: " + request.analysisMode;
        return false;
    }

    return true;
}

CoreAnalysisResult CoreAnalysisFacade::run(const CoreAnalysisRequest& request)
{
    CoreAnalysisResult result;

    if (!validateRequest(request, result)) {
        return result;
    }

    RunRequest runReq;
    runReq.inputPaths.push_back(request.filePath);

    if (!request.channelName.empty()) {
        std::vector<size_t> selectedChannels;
        std::string channelErr;

        if (!tryResolveSelectedChannel(
            request.filePath,
            request.channelName,
            selectedChannels,
            channelErr)) {
            result.success = false;
            result.message = channelErr;
            return result;
        }

        if (!selectedChannels.empty()) {
            runReq.atfxSelectedChannelsByFile.push_back(selectedChannels);
        }
    }

    runReq.mode = toAnalysisMode(request.analysisMode);

    runReq.fftParams.block_size = request.fftSize;
    runReq.fftParams.overlap_ratio = request.overlap;

    if (request.weighting == "A" || request.weighting == "a") {
        runReq.fftParams.weight_type = Weighting::WeightType::A;
    }
    else if (request.weighting == "C" || request.weighting == "c") {
        runReq.fftParams.weight_type = Weighting::WeightType::C;
    }
    else {
        runReq.fftParams.weight_type = Weighting::WeightType::None;
    }

    runReq.fftParams.time_weighting = request.timeWeighting;
    runReq.fftParams.level_time_constant_sec = request.levelTimeConstantSec;
    runReq.fftParams.level_window_sec = request.levelWindowSec;
    runReq.fftParams.level_output_step_sec = request.levelOutputStepSec;

    if (runReq.mode == AnalysisMode::OCTAVE_1_1) {
        runReq.fftParams.bandsPerOctave = 1;
    }
    else if (runReq.mode == AnalysisMode::OCTAVE_1_3) {
        runReq.fftParams.bandsPerOctave = 3;
    }

    runReq.rpmBinStep = request.rpmBinStep;
    if (!request.rpmChannelName.empty()) {
        runReq.rpmChannelNameByFile.push_back(request.rpmChannelName);
    }

    runReq.maxThreads = request.maxThreads;
    runReq.maxRetries = request.maxRetries;
    runReq.enableCancel = request.enableCancel;

    MappedRunConfig mapped = MapRunRequest(runReq);

    BuildResponse buildResp = TaskBuilder::BuildFromRequest(mapped.buildReq);
    if (!buildResp.ok) {
        result.success = false;
        result.message = "TaskBuilder failed: " + buildResp.message;
        return result;
    }

    mapped.engineCfg.files = std::move(buildResp.files);
    mapped.engineCfg.jobs = std::move(buildResp.jobs);

    FFT11Engine engine;
    EngineRunResult engineResult = engine.Run(mapped.engineCfg);

    result.success = (engineResult.failed == 0 && !engineResult.results.empty());
    result.message = result.success ? "OK" : "Engine finished with failures.";

    for (const auto& jr : engineResult.results) {
        if (!jr.timeCsvPath.empty()) {
            result.timeSignalCsv = jr.timeCsvPath;
            result.generatedFiles.push_back(jr.timeCsvPath);
        }

        if (!jr.fftCsvPath.empty()) {
            result.fftCsv = jr.fftCsvPath;
            result.generatedFiles.push_back(jr.fftCsvPath);
        }

        if (!jr.levelVsTimeCsvPath.empty()) {
            result.levelVsTimeCsv = jr.levelVsTimeCsvPath;
            result.generatedFiles.push_back(jr.levelVsTimeCsvPath);
        }

        result.peakFrequency = jr.peak.freq;
        result.peakValue = jr.peak.mag;

        if (!jr.ok && !jr.message.empty()) {
            result.message = jr.message;
        }
    }

    if (runReq.mode == AnalysisMode::FFT_VS_TIME) {
        result.spectrogramCsv = result.fftCsv;
    }
    else if (runReq.mode == AnalysisMode::OCTAVE_1_1 ||
        runReq.mode == AnalysisMode::OCTAVE_1_3) {
        result.octaveCsv = result.fftCsv;
    }

    return result;
}