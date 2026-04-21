#include "TaskBuilder.h"
#include "../domain/ParseUtils.h"
#include "io/ATFXReader.h"
#include "io/HDFReader.h"

#include <set>
#include <string>
#include <vector>

static ATFXChannelInfo ToATFXLike(const HDFChannelInfo& h)
{
    ATFXChannelInfo a;
    a.channelName = h.channelName;
    a.dataType = h.dataType;
    a.dataLength = h.dataLength;
    a.dataOffset = h.dataOffset;
    a.channelLabel = h.channelLabel.empty() ? h.channelName : h.channelLabel;
    a.unit = h.unit;
    a.dof = h.dof.empty() ? "-" : h.dof;
    return a;
}

BuildResponse TaskBuilder::BuildFromRequest(const BuildRequest& req)
{
    BuildResponse out;
    out.ok = false;
    out.message = "unknown";

    if (req.inputPaths.empty()) {
        out.message = "inputPaths is empty";
        return out;
    }

    // 是否需要 RPM 配置（FFT_VS_RPM / LEVEL_VS_RPM）
    const bool needRpm =
        (req.mode == AnalysisMode::FFT_VS_RPM || req.mode == AnalysisMode::LEVEL_VS_RPM);

    // 1) 构建 files
    out.files.clear();
    out.files.reserve(req.inputPaths.size());

    for (const auto& p : req.inputPaths) {
        FileItem f;
        f.path = p;
        f.ext = get_ext_lower(p);
        f.selected = req.selectAllFiles;
        out.files.push_back(f);
    }

    // 2) 读取通道列表（ATFX + HDF）
    for (size_t fi = 0; fi < out.files.size(); ++fi) {
        auto& f = out.files[fi];
        if (!f.selected) continue;

        // ---------- ATFX ----------
        if (f.ext == "atfx") {
            FFT11_ATFXReader atfx;
            if (!atfx.GetAllChannels(f.path, f.channels, f.fs) || f.channels.empty()) {
                f.selected = false;
                continue;
            }

            f.selectedChannels.clear();

            const bool hasCustom =
                (fi < req.atfxSelectedChannelsByFile.size() &&
                    !req.atfxSelectedChannelsByFile[fi].empty());

            if (hasCustom) {
                std::set<size_t> uniq;
                for (size_t ci : req.atfxSelectedChannelsByFile[fi]) {
                    if (ci < f.channels.size()) {
                        uniq.insert(ci);
                    }
                }
                f.selectedChannels.assign(uniq.begin(), uniq.end());
            }

            if (f.selectedChannels.empty()) {
                for (size_t ci = 0; ci < f.channels.size(); ++ci) {
                    f.selectedChannels.push_back(ci);
                }
            }
        }
        // ---------- HDF ----------
        else if (f.ext == "hdf") {
            FFT11_HDFReader hdf;
            std::vector<HDFChannelInfo> hdfChannels;
            double fs = 0.0;

            if (!hdf.GetAllChannels(f.path, hdfChannels, fs) || hdfChannels.empty()) {
                f.selected = false;
                continue;
            }

            f.channels.clear();
            f.channels.reserve(hdfChannels.size());
            for (const auto& hc : hdfChannels) {
                f.channels.push_back(ToATFXLike(hc));
            }
            f.fs = fs;

            f.selectedChannels.clear();

            const bool hasCustom =
                (fi < req.atfxSelectedChannelsByFile.size() &&
                    !req.atfxSelectedChannelsByFile[fi].empty());

            if (hasCustom) {
                std::set<size_t> uniq;
                for (size_t ci : req.atfxSelectedChannelsByFile[fi]) {
                    if (ci < f.channels.size()) {
                        uniq.insert(ci);
                    }
                }
                f.selectedChannels.assign(uniq.begin(), uniq.end());
            }

            if (f.selectedChannels.empty()) {
                for (size_t ci = 0; ci < f.channels.size(); ++ci) {
                    f.selectedChannels.push_back(ci);
                }
            }
        }
    }

    // 3) 构建 jobs
    out.jobs.clear();

    for (size_t fi = 0; fi < out.files.size(); ++fi) {
        const auto& f = out.files[fi];
        if (!f.selected) continue;

        // ---------- ATFX ----------
        if (f.ext == "atfx") {
            for (size_t ci : f.selectedChannels) {
                if (ci >= f.channels.size()) continue;

                Job j;
                j.fileIdx = fi;
                j.isATFX = true;
                j.channelIdx = ci;
                j.mode = req.mode;
                j.params = req.fftParams;

                // LEVEL 模式统一禁用频率计权（仅时间计权）
                if (j.mode == AnalysisMode::LEVEL_VS_RPM) {
                    j.params.weight_type = Weighting::WeightType::None;
                }

                if (needRpm) {
                    if (fi >= req.rpmChannelNameByFile.size()) continue;

                    const std::string& rpmName = req.rpmChannelNameByFile[fi];
                    if (rpmName.empty()) continue;

                    j.rpmChannelName = rpmName;
                    j.rpmBinStep = (req.rpmBinStep > 0.0 ? req.rpmBinStep : 50.0);
                }

                out.jobs.push_back(j);
            }
        }
        // ---------- HDF ----------
        else if (f.ext == "hdf") {
            for (size_t ci : f.selectedChannels) {
                if (ci >= f.channels.size()) continue;

                Job j;
                j.fileIdx = fi;
                j.isATFX = false;
                j.channelIdx = ci;
                j.mode = req.mode;
                j.params = req.fftParams;

                // LEVEL 模式统一禁用频率计权（仅时间计权）
                if (j.mode == AnalysisMode::LEVEL_VS_RPM) {
                    j.params.weight_type = Weighting::WeightType::None;
                }

                if (needRpm) {
                    if (fi >= req.rpmChannelNameByFile.size()) continue;

                    const std::string& rpmName = req.rpmChannelNameByFile[fi];
                    if (rpmName.empty()) continue;

                    j.rpmChannelName = rpmName;
                    j.rpmBinStep = (req.rpmBinStep > 0.0 ? req.rpmBinStep : 50.0);
                }

                out.jobs.push_back(j);
            }
        }
        // ---------- WAV ----------
        else if (f.ext == "wav") {
            if (needRpm) {
                // 当前 RPM 模式仅支持 ATFX/HDF（需RPM通道）
                continue;
            }

            Job j;
            j.fileIdx = fi;
            j.isATFX = false;
            j.channelIdx = 0;
            j.mode = req.mode;
            j.params = req.fftParams;

            out.jobs.push_back(j);
        }
    }

    if (out.jobs.empty()) {
        out.message = "no jobs built";
        return out;
    }

    out.ok = true;
    out.message = "OK";
    return out;
}