#pragma once

#include "../domain/Types.h"
#include "../io/ATFXReader.h"
#include "../io/HDFReader.h"
#include <string>
#include <vector>

class DataReaderService {
public:
    // 统一读取入口：根据 file/job 自动读取 WAV/HDF/ATFX
    bool ReadSignal(const Job& job,
        const FileItem& file,
        SignalData& out,
        std::string& err) const;

private:
    bool ReadATFX(const Job& job,
        const FileItem& file,
        SignalData& out,
        std::string& err) const;

    bool ReadHDF(const Job& job,
        const FileItem& file,
        SignalData& out,
        std::string& err) const;

    bool ReadWAV(const FileItem& file,
        SignalData& out,
        std::string& err) const;
};