param(
    [string]$Dist = 'dist',
    [string]$Baseline = "$PSScriptRoot/release-baseline.json",
    [string]$Report = '.work/release-report.md',
    [string[]]$Corpus = @('tests/issue-6132.pdf', 'tests/xmp-properties.pdf'),
    [int]$TimeoutSeconds = 60
)

$ErrorActionPreference = 'Stop'
$lines = [Collections.Generic.List[string]]::new()
$lines.Add('# Release size / performance report')
$failed = $false
$limit = 1.10
try {
    $record = Get-Content -LiteralPath $Baseline -Raw | ConvertFrom-Json
    $lines.Add("Baseline: $($record.source)")
    foreach ($asset in $record.assets.PSObject.Properties) {
        $path = Join-Path $Dist $asset.Name
        if (!(Test-Path -LiteralPath $path)) {
            $lines.Add("$($asset.Name): unavailable")
            continue
        }
        $bytes = (Get-Item -LiteralPath $path).Length
        if ($asset.Value -le 0) { throw 'Invalid baseline size' }
        $growth = $bytes / $asset.Value - 1
        $lines.Add(('{0}: {1} bytes; baseline {2}; growth {3:P2}' -f $asset.Name, $bytes, $asset.Value, $growth))
        if ($bytes -gt $asset.Value * $limit) { $failed = $true }
    }
} catch {
    $lines.Add("Size report unavailable: $_")
}

$exe = Join-Path $Dist 'Swiftleaf.exe'
$lines.Add('Performance is advisory. Fresh-process engine load + first render is a usable-page proxy; OS cache is not flushed. P95 is sequential page load + render, not UI navigation latency. Memory is sampled every 20 ms. External Office/OCR resources are excluded; account for them separately.')
if (Test-Path -LiteralPath $exe) {
    foreach ($pdf in $Corpus) {
        $proc = $null
        try {
            if (!(Test-Path -LiteralPath $pdf)) { throw 'Corpus file missing' }
            $log = Join-Path ([IO.Path]::GetTempPath()) ([IO.Path]::GetRandomFileName())
            $args = '-for-testing -bench "{0}" 1-50 -log-to-file "{1}"' -f (Resolve-Path -LiteralPath $pdf).Path, $log
            $proc = Start-Process -FilePath (Resolve-Path -LiteralPath $exe).Path -ArgumentList $args -WindowStyle Hidden -PassThru
            $clock = [Diagnostics.Stopwatch]::StartNew()
            $working = 0L
            $private = 0L
            while (!$proc.HasExited) {
                $proc.Refresh()
                $working = [Math]::Max($working, $proc.WorkingSet64)
                $private = [Math]::Max($private, $proc.PrivateMemorySize64)
                if ($clock.Elapsed.TotalSeconds -gt $TimeoutSeconds) { throw 'Benchmark timed out' }
                Start-Sleep -Milliseconds 20
            }
            $text = Get-Content -LiteralPath $log -Raw
            if ($proc.ExitCode -ne 0 -or $text -match 'Error:') { throw 'Benchmark failed; inspect corpus / log' }
            $load = [regex]::Match($text, '(?m)^load: ([\d.]+) ms')
            $renders = [regex]::Matches($text, 'pageload\s+\d+: ([\d.]+) ms\s+pagerender\s+\d+: ([\d.]+) ms')
            if (!$load.Success -or !$renders.Count) { throw 'No timing samples' }
            $samples = @($renders | ForEach-Object { [double]::Parse($_.Groups[1].Value, [Globalization.CultureInfo]::InvariantCulture) + [double]::Parse($_.Groups[2].Value, [Globalization.CultureInfo]::InvariantCulture) })
            $first = [double]::Parse($load.Groups[1].Value, [Globalization.CultureInfo]::InvariantCulture) + $samples[0]
            $sorted = @($samples | Sort-Object)
            $p95 = $sorted[[Math]::Ceiling($sorted.Count * .95) - 1]
            $hash = (Get-FileHash -LiteralPath $pdf -Algorithm SHA256).Hash
            $lines.Add("$pdf (SHA256 $hash): first-page proxy $first ms; P95 $p95 ms; samples $($samples.Count); working set $working bytes; private $private bytes")
        } catch {
            $lines.Add("${pdf}: performance unavailable: $_")
        } finally {
            if ($proc -and !$proc.HasExited) { Stop-Process -Id $proc.Id -ErrorAction SilentlyContinue }
            if ($log -and (Test-Path -LiteralPath $log)) { Remove-Item -LiteralPath $log }
        }
    }
}
$lines.Add('Only asset growth strictly above 10% fails this gate. Missing measurements are reported, never represented as zero.')
$parent = Split-Path -Parent $Report
if ($parent) { New-Item -ItemType Directory -Force -Path $parent | Out-Null }
$lines | Set-Content -LiteralPath $Report
$lines | Write-Output
if ($env:GITHUB_STEP_SUMMARY) { $lines | Add-Content -LiteralPath $env:GITHUB_STEP_SUMMARY }
if ($failed) { exit 1 }
exit 0
