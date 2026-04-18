#pragma once
#ifndef FFT11_OCTAVE_ANALYZER_H
#define FFT11_OCTAVE_ANALYZER_H

#include <vector>
#include <string>
#include "preprocess/Window.h" // for Window::WindowType

enum class OctaveBandType {
    Full,   // 1/1 octave
    Third   // 1/3 octave
};

struct OctaveBandResult {
    std::vector<double> bandCenters;
    std::vector<double> bandLows;
    std::vector<double> bandHighs;
    std::vector<double> bandValues;    // linear RMS (Pa)
    std::vector<double> bandValues_dB; // dB SPL
};

// Frequency-domain (FFT amplitude) interface
// avgMag: single-sided amplitudes A_k (length = nfft/2 + 1) as returned by compute_averaged_fft
// fs: sampling rate
// nfft: FFT length used to produce avgMag
// block_size: analysis window length (N) used in compute_averaged_fft (needed for window stats)
// wtype: window type used in compute_averaged_fft
// bandType/ref/calibrationFactor: same semantics as before
OctaveBandResult compute_octave_bands_from_fft(
    const std::vector<double>& avgMag,
    double fs,
    size_t nfft,
    size_t block_size,
    Window::WindowType wtype,
    OctaveBandType bandType,
    double ref = 20e-6,
    double calibrationFactor = 1.0
);

void write_octave_csv(const OctaveBandResult& result, const std::string& path);

// Return center frequencies for Full or Third octave
const std::vector<double>& get_octave_centers(OctaveBandType bandType);

#endif // FFT11_OCTAVE_ANALYZER_H
