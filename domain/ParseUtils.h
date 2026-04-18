#pragma once
#include <string>
#include <vector>
#include "domain/Types.h"

std::string get_ext_lower(const std::string& fn);
std::string trim_copy(const std::string& s);
std::vector<size_t> parse_indices_1based(const std::string& input, size_t maxCount);
bool ask_reuse_default_yes(const std::string& msg);
FFTParams AskFFTParams(const FFTParams& d = FFTParams());
FFTParams AskFFTParamsWithTitle(const std::string& title, const FFTParams& d = FFTParams());
bool LoadSpectrumFromCsv(const std::string& csvPath, std::vector<double>& outFreq, std::vector<double>& outMag);
PeakPreview CalcPeakFromCsvData(const std::vector<double>& freq, const std::vector<double>& mag);