param(
    [string]$Root = ".",
    [switch]$Apply = $false
)

Write-Host "=== START ==="
Write-Host "Root: $Root  Apply: $Apply"

$targets = Get-ChildItem -Path $Root -Recurse -File -ErrorAction SilentlyContinue |
Where-Object { $_.Name -match '^FFT11_(.+)\.(h|hpp|c|cc|cpp|cxx)$' }

Write-Host "Matched: $($targets.Count)"

if ($targets.Count -eq 0) {
    Write-Host "No FFT11_ files found."
    Write-Host "=== END ==="
    exit
}

$targets | Select-Object -First 50 FullName | ForEach-Object { Write-Host $_.FullName }

if (-not $Apply) {
    Write-Host "Dry-run only. Use -Apply to execute rename."
    Write-Host "=== END ==="
    exit
}

foreach ($f in $targets) {
    $newName = $f.Name -replace '^FFT11_', ''
    $newPath = Join-Path $f.DirectoryName $newName

    if (-not (Test-Path $newPath)) {
        Move-Item -LiteralPath $f.FullName -Destination $newPath
        Write-Host "RENAMED: $($f.Name) -> $newName"
    } else {
        Write-Warning "SKIP exists: $newPath"
    }
}

Write-Host "=== END ==="
