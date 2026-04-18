param([string]$Root = ".")

Write-Host "=== START Update-Include-And-CMake ==="
Write-Host "Root: $Root"

$map = @{
"FFT11_common.h"="common.h"
"FFT11_Utils.h"="Utils.h"
"FFT11_Utils.cpp"="Utils.cpp"
"FFT11_Engine.h"="Engine.h"
"FFT11_Engine.cpp"="Engine.cpp"
"FFT11_FFTvsRpmMapper.cpp"="FFTvsRpmMapper.cpp"
"FFT11_FFTvsRpmMapper.h"="FFTvsRpmMapper.h"
"FFT11_FFTvsTimeAnalyzer.cpp"="FFTvsTimeAnalyzer.cpp"
"FFT11_FFTvsTimeAnalyzer.h"="FFTvsTimeAnalyzer.h"
"FFT11_OctaveAnalyzer.cpp"="OctaveAnalyzer.cpp"
"FFT11_OctaveAnalyzer.h"="OctaveAnalyzer.h"
"FFT11_Analyzer.cpp"="Analyzer.cpp"
"FFT11_Analyzer.h"="Analyzer.h"
"FFT11_FFTCore.cpp"="FFTCore.cpp"
"FFT11_FFTCore.h"="FFTCore.h"
"FFT11_ATFXReader.cpp"="ATFXReader.cpp"
"FFT11_ATFXReader.h"="ATFXReader.h"
"FFT11_HDFReader.cpp"="HDFReader.cpp"
"FFT11_HDFReader.h"="HDFReader.h"
"FFT11_WAVReader.cpp"="WAVReader.cpp"
"FFT11_WAVReader.h"="WAVReader.h"
"FFT11_AudioBlocking.cpp"="AudioBlocking.cpp"
"FFT11_AudioBlocking.h"="AudioBlocking.h"
"FFT11_Preprocessing.cpp"="Preprocessing.cpp"
"FFT11_Preprocessing.h"="Preprocessing.h"
"FFT11_Weighting.cpp"="Weighting.cpp"
"FFT11_Weighting.h"="Weighting.h"
"FFT11_Window.cpp"="Window.cpp"
"FFT11_Window.h"="Window.h"
}

$files = Get-ChildItem -Path $Root -Recurse -File -ErrorAction SilentlyContinue |
Where-Object { $_.Extension -in ".h",".hpp",".c",".cc",".cpp",".cxx",".txt",".cmake" -or $_.Name -eq "CMakeLists.txt" }

Write-Host "Scanned files: $($files.Count)"

$updated = 0
foreach ($f in $files) {
    $content = Get-Content -LiteralPath $f.FullName -Raw
    $orig = $content

    foreach ($k in $map.Keys) {
        $content = $content.Replace($k, $map[$k])
    }

    if ($content -ne $orig) {
        Set-Content -LiteralPath $f.FullName -Value $content -Encoding UTF8
        $updated++
        Write-Host "UPDATED: $($f.FullName)"
    }
}

Write-Host "Done. Updated files: $updated"
Write-Host "=== END ==="
