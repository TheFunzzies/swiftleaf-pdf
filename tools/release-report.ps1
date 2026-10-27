param(
    [string]$Exe = 'out/rel64/Swiftleaf.exe',
    [string]$Baseline = 'tools/release-baseline.json',
    [string]$Report = 'dist/release-report.json'
)
$ErrorActionPreference = 'Stop'
$binary = Get-Item -LiteralPath $Exe
$base = Get-Content -LiteralPath $Baseline -Raw | ConvertFrom-Json
if ($base.bytes -le 0) { throw 'Invalid recorded size baseline' }
$result = [ordered]@{
    bytes = $binary.Length
    baselineBytes = $base.bytes
    baselineSource = $base.source
    growthPercent = [math]::Round(100 * ($binary.Length / $base.bytes - 1), 2)
    sha256 = (Get-FileHash -LiteralPath $binary.FullName -Algorithm SHA256).Hash
    benchmarkMs = $null
    peakWorkingSetBytes = $null
    benchmarkStatus = 'not run'
}
# Deterministic one-page fixture. Benchmark mode renders then exits.
$fixture = Join-Path $env:TEMP "swiftleaf-release-$PID.pdf"
$objects = @('<< /Type /Catalog /Pages 2 0 R >>', '<< /Type /Pages /Kids [3 0 R] /Count 1 >>', '<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] >>')
$pdf = "%PDF-1.4`n"
$offsets = @(0)
for ($i = 0; $i -lt $objects.Count; $i++) {
    $offsets += $pdf.Length
    $pdf += "$($i + 1) 0 obj`n$($objects[$i])`nendobj`n"
}
$xref = $pdf.Length
$pdf += "xref`n0 4`n0000000000 65535 f `n"
foreach ($offset in $offsets[1..3]) { $pdf += ('{0:0000000000} 00000 n ' -f $offset) + "`n" }
$pdf += "trailer`n<< /Size 4 /Root 1 0 R >>`nstartxref`n$xref`n%%EOF`n"
[IO.File]::WriteAllText($fixture, $pdf, [Text.Encoding]::ASCII)
$process = $null
try {
    $timer = [Diagnostics.Stopwatch]::StartNew()
    $process = Start-Process -FilePath $binary.FullName -ArgumentList @('-for-testing', '-bench', "`"$fixture`"", '1') -PassThru -WindowStyle Hidden
    $peak = 0L
    while (!$process.HasExited -and $timer.Elapsed.TotalSeconds -lt 30) {
        $process.Refresh()
        $peak = [math]::Max($peak, $process.PeakWorkingSet64)
        Start-Sleep -Milliseconds 50
    }
    if (!$process.HasExited) {
        $process.Kill()
        $result.benchmarkStatus = 'timeout (report only)'
    } else {
        $result.benchmarkMs = $timer.ElapsedMilliseconds
        $result.peakWorkingSetBytes = $peak
        $result.benchmarkStatus = "exit $($process.ExitCode); one blank page, includes process startup"
    }
} catch {
    $result.benchmarkStatus = "unavailable (report only): $_"
} finally {
    if ($process) { $process.Dispose() }
    Remove-Item -LiteralPath $fixture -ErrorAction SilentlyContinue
}
New-Item -ItemType Directory -Force (Split-Path $Report) | Out-Null
$result | ConvertTo-Json | Set-Content -LiteralPath $Report
Get-Content -LiteralPath $Report
if ($env:GITHUB_STEP_SUMMARY) {
    "### Release size/performance`n``````json`n$($result | ConvertTo-Json)`n```````n" | Add-Content $env:GITHUB_STEP_SUMMARY
}
if ($binary.Length -gt $base.bytes * 1.10) { throw 'Release size exceeds recorded baseline by more than 10%' }
