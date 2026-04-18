param(
    [string]$Root = "."
)

$extensions = @("*.h","*.hpp","*.c","*.cpp","*.cc","*.cxx")
$files = Get-ChildItem -Path $Root -Recurse -File -Include $extensions

# UTF-8（严格）检测器：遇到非法字节会抛异常
$utf8Strict = New-Object System.Text.UTF8Encoding($false, $true)

$result = foreach ($f in $files) {
    $bytes = [System.IO.File]::ReadAllBytes($f.FullName)

    $isUtf8 = $true
    try {
        [void]$utf8Strict.GetString($bytes)
    } catch {
        $isUtf8 = $false
    }

    # 粗略判断 BOM
    $hasUtf8Bom = $bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF

    [PSCustomObject]@{
        File       = $f.FullName
        Size       = $bytes.Length
        Utf8Valid  = $isUtf8
        Utf8Bom    = $hasUtf8Bom
        Suggestion = if ($isUtf8) { "Keep" } else { "ConvertFrom936ToUtf8" }
    }
}

$report = Join-Path (Resolve-Path $Root) "encoding-report.csv"
$result | Export-Csv -Path $report -NoTypeInformation -Encoding UTF8

Write-Host "Done. Report: $report"
Write-Host "Tip: Open CSV in Excel/VSCode and check Suggestion column."