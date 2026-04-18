param(
    [string]$Root = ".",
    [switch]$WithBOM = $true,
    [switch]$OnlyNonUtf8 = $true,
    [switch]$DryRun = $true
)

Write-Host "=== Script started ==="
Write-Host "Root: $Root"
Write-Host "WithBOM: $WithBOM | OnlyNonUtf8: $OnlyNonUtf8 | DryRun: $DryRun"

$ext = @("*.h","*.hpp","*.c","*.cpp","*.cc","*.cxx")
$files = Get-ChildItem -Path $Root -Recurse -File -Include $ext -ErrorAction SilentlyContinue
Write-Host "Matched files: $($files.Count)"

if ($files.Count -eq 0) {
    Write-Warning "No source files found. Check Root path."
    exit
}

$srcEnc = [System.Text.Encoding]::GetEncoding(936)
$utf8Strict = New-Object System.Text.UTF8Encoding($false, $true)
$dstEnc = if ($WithBOM) { New-Object System.Text.UTF8Encoding($true) } else { New-Object System.Text.UTF8Encoding($false) }

$converted=0; $skipped=0; $failed=0; $shown=0

foreach ($f in $files) {
    try {
        $bytes = [System.IO.File]::ReadAllBytes($f.FullName)

        $isUtf8 = $true
        try { [void]$utf8Strict.GetString($bytes) } catch { $isUtf8 = $false }

        if ($OnlyNonUtf8 -and $isUtf8) { $skipped++; continue }

        if ($shown -lt 20) {
            Write-Host ("TARGET: " + $f.FullName)
            $shown++
        }

        if (-not $DryRun) {
            $bak = "$($f.FullName).bak"
            if (-not (Test-Path $bak)) { [System.IO.File]::WriteAllBytes($bak, $bytes) }

            $text = $srcEnc.GetString($bytes)
            [System.IO.File]::WriteAllText($f.FullName, $text, $dstEnc)
            $converted++
        }
    } catch {
        $failed++
        Write-Warning "Failed: $($f.FullName)"
    }
}

Write-Host "=== Summary ==="
Write-Host "DryRun: $DryRun"
Write-Host "WouldConvert/Converted: $(if($DryRun){$shown}else{$converted})"
Write-Host "Skipped(UTF8): $skipped"
Write-Host "Failed: $failed"
Write-Host "=== Script finished ==="