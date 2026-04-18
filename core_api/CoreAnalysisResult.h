#pragma once
#include <string>
#include <vector>

struct CoreAnalysisResult
{
    bool success = false;
    std::string message;

    std::string timeSignalCsv;
    std::string fftCsv;
    std::string spectrogramCsv;
    std::string octaveCsv;
    std::string reportFile;
    std::string levelVsTimeCsv;

    double peakFrequency = 0.0;
    double peakValue = 0.0;

    std::vector<std::string> generatedFiles;
};