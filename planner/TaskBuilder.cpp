#include "TaskBuilder.h"
#include "../domain/HdfChannelUtils.h"
#include "../domain/ParseUtils.h"
#include "../domain/FileTypeUtils.h"
#include "io/ATFXReader.h"
#include "io/HDFReader.h"

#include <set>

static ATFXChannelInfo ToATFXLike(const HDFChannelInfo& h) {
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

BuildResponse TaskBuilder::BuildFromRequest(const BuildRequest& req) {
    BuildResponse out;
    out.ok = false;
    out.message = "unknown";

    if (req.inputPaths.empty()) {
        out.message = "inputPaths is empty";
        return out;
    }

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
                f.selected = false; // 读取失败跳过
                continue;
            }

            f.selectedChannels.clear();

            bool hasCustom = (fi < req.atfxSelectedChannelsByFile.size() &&
                !req.atfxSelectedChannelsByFile[fi].empty());

            if (hasCustom) {
                std::set<size_t> uniq;
                for (size_t ci : req.atfxSelectedChannelsByFile[fi]) {
                    if (ci < f.channels.size()) uniq.insert(ci);
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
        else if (IsHdfExt(f.ext)) {
            FFT11_HDFReader hdf;
            std::vector<HDFChannelInfo> hdfChannels;
            double fs = 0.0;

            if (!hdf.GetAllChannels(f.path, hdfChannels, fs) || hdfChannels.empty()) {
                f.selected = false; // 读取失败跳过
                continue;
            }

            // 映射到 FileItem.channels (ATFXChannelInfo)
            f.channels.clear();
            f.channels.reserve(hdfChannels.size());
            for (const auto& hc : hdfChannels) {
                f.channels.push_back(ToATFXLike(hc));
            }
            f.fs = fs;

            f.selectedChannels.clear();

            // 复用 req.atfxSelectedChannelsByFile 作为“文件->通道索引”通用输入
            bool hasCustom = (fi < req.atfxSelectedChannelsByFile.size() &&
                !req.atfxSelectedChannelsByFile[fi].empty());

            if (hasCustom) {
                std::set<size_t> uniq;
                for (size_t ci : req.atfxSelectedChannelsByFile[fi]) {
                    if (ci < f.channels.size()) uniq.insert(ci);
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
    bool missingHdfRpmChannel = false;

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

                if (req.mode == AnalysisMode::FFT_VS_RPM) {
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
        else if (IsHdfExt(f.ext)) {
            for (size_t ci : f.selectedChannels) {
                if (ci >= f.channels.size()) continue;

                Job j;
                j.fileIdx = fi;
                j.isATFX = false;   // 在执行器中通过 file.ext=="hdf" 分支处理
                j.channelIdx = ci;
                j.mode = req.mode;
                j.params = req.fftParams;

                if (req.mode == AnalysisMode::FFT_VS_RPM) {
                    std::string rpmName;
                    if (fi < req.rpmChannelNameByFile.size()) {
                        rpmName = req.rpmChannelNameByFile[fi];
                    }
                    if (rpmName.empty()) {
                        rpmName = HdfChannelUtils::DetectRpmChannelName(f.channels);
                    }
                    if (rpmName.empty()) {
                        missingHdfRpmChannel = true;
                        continue;
                    }

                    j.rpmChannelName = rpmName;
                    j.rpmBinStep = (req.rpmBinStep > 0.0 ? req.rpmBinStep : 50.0);
                }

                out.jobs.push_back(j);
            }
        }

        // ---------- WAV ----------
        else if (f.ext == "wav") {
            // FFT vs rpm 暂不支持 WAV
            if (req.mode == AnalysisMode::FFT_VS_RPM) continue;

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
        out.message = missingHdfRpmChannel
            ? "no jobs built: missing rpm channel for hdf/h5 file"
            : "no jobs built";
        return out;
    }

    out.ok = true;
    out.message = "OK";
    return out;
}
