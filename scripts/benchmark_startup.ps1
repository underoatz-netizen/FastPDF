# benchmark_startup.ps1 - reproducible direct-CLI startup benchmark for FastPDF.
#
# Launches FastPDF.exe <pdf> with the benchmark instrumentation explicitly
# enabled (FASTPDF_BENCHMARK=1) and collects the JSON phase output for N runs.
# Reports cold (first run) and warm (remaining runs) time-to-first-presented-
# frame statistics (the "first_frame_presented" phase, recorded at the actual
# EndDraw frame).
#
# This harness measures FastPDF's own startup/open/first-frame phases only. It
# does NOT compare against other products; any cross-product comparison must be
# run with a matched methodology outside this script.
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File scripts/benchmark_startup.ps1 `
#       -PdfPath <path-to.pdf> [-ExePath <path-to-FastPDF.exe>] `
#       [-Runs 10] [-TimeoutSec 60]

param(
    [Parameter(Mandatory = $true)][string]$PdfPath,
    [string]$ExePath = "",
    [int]$Runs = 10,
    [int]$TimeoutSec = 60
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path -LiteralPath $PdfPath)) {
    Write-Error "PDF not found: $PdfPath"
    exit 1
}

if ($ExePath -eq "") {
    $candidates = @(
        "build/release/bin/Release/FastPDF.exe",
        "build/debug/bin/Debug/FastPDF.exe"
    )
    foreach ($c in $candidates) {
        if (Test-Path -LiteralPath $c) {
            $ExePath = (Resolve-Path -LiteralPath $c).Path
            break
        }
    }
}
if ($ExePath -eq "" -or -not (Test-Path -LiteralPath $ExePath)) {
    Write-Error "FastPDF.exe not found; pass -ExePath."
    exit 1
}

$outDir = Join-Path $env:TEMP "fastpdf_benchmark"
New-Item -ItemType Directory -Force -Path $outDir | Out-Null

$results = @()  # each: run, cold, firstFrameMs, phases

for ($i = 1; $i -le $Runs; $i++) {
    $jsonPath = Join-Path $outDir ("benchmark_run_{0:D2}.json" -f $i)
    Remove-Item -LiteralPath $jsonPath -ErrorAction SilentlyContinue

    $env:FASTPDF_BENCHMARK = "1"
    $env:FASTPDF_BENCHMARK_OUT = $jsonPath

    $proc = Start-Process -FilePath $ExePath -ArgumentList "`"$PdfPath`"" -PassThru

    # Poll for the JSON output (written after the first frame is presented).
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    $json = $null
    while ((Get-Date) -lt $deadline) {
        if (Test-Path -LiteralPath $jsonPath) {
            try {
                $json = Get-Content -LiteralPath $jsonPath -Raw | ConvertFrom-Json
                break
            } catch {
                # File may be mid-write; retry.
            }
        }
        Start-Sleep -Milliseconds 25
    }

    if ($json -eq $null) {
        Write-Warning ("Run {0}: no benchmark JSON within {1}s (app may have failed to open the PDF)." -f $i, $TimeoutSec)
        if (-not $proc.HasExited) { $proc.CloseMainWindow() | Out-Null }
        $results += @{ run = $i; cold = ($i -eq 1); firstFrameMs = $null; phases = $null }
        continue
    }

    $phases = @{}
    foreach ($p in $json.phases) {
        $phases[$p.name] = [double]$p.ms
    }
    $firstFrameMs = $null
    if ($phases.ContainsKey("first_frame_presented")) {
        $firstFrameMs = $phases["first_frame_presented"]
    }

    $results += @{ run = $i; cold = ($i -eq 1); firstFrameMs = $firstFrameMs; phases = $phases }

    # Close the app gracefully so the destructor can finish.
    if (-not $proc.HasExited) {
        $proc.CloseMainWindow() | Out-Null
        if (-not $proc.WaitForExit(5000)) {
            Stop-Process -Id $proc.Id -Force
        }
    }
}

Remove-Item Env:FASTPDF_BENCHMARK -ErrorAction SilentlyContinue
Remove-Item Env:FASTPDF_BENCHMARK_OUT -ErrorAction SilentlyContinue

Write-Output "FastPDF startup benchmark: $PdfPath"
Write-Output ("Executable: {0}" -f $ExePath)
Write-Output ("Runs: {0}" -f $Runs)
Write-Output ""

$cold = @($results | Where-Object { $_.cold -and $_.firstFrameMs -ne $null })
$warm = @($results | Where-Object { -not $_.cold -and $_.firstFrameMs -ne $null })

function Summarize($label, $values) {
    if ($values.Count -eq 0) {
        Write-Output ("{0}: no valid runs" -f $label)
        return
    }
    $sorted = @($values | Sort-Object)
    $median = $sorted[[int]($sorted.Count / 2)]
    $mean = ($values | Measure-Object -Average).Average
    $min = $sorted[0]
    $max = $sorted[-1]
    Write-Output ("{0}: n={1} median={2:N1}ms mean={3:N1}ms min={4:N1}ms max={5:N1}ms" -f `
        $label, $values.Count, $median, $mean, $min, $max)
}

if ($cold.Count -gt 0) {
    Summarize "Cold (run 1) first-frame" @($cold | ForEach-Object { $_.firstFrameMs })
} else {
    Write-Output "Cold (run 1): no valid measurement"
}
if ($warm.Count -gt 0) {
    Summarize "Warm (runs 2+) first-frame" @($warm | ForEach-Object { $_.firstFrameMs })
} else {
    Write-Output "Warm (runs 2+): no valid measurement"
}

Write-Output ""
Write-Output "Per-run first-frame (ms):"
foreach ($r in $results) {
    $tag = if ($r.cold) { "cold" } else { "warm" }
    $val = if ($r.firstFrameMs -ne $null) { ("{0:N1}" -f $r.firstFrameMs) } else { "n/a" }
    Write-Output ("  run {0:D2} [{1}]: {2}" -f $r.run, $tag, $val)
}