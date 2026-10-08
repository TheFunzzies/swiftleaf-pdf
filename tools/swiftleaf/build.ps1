# Build Swiftleaf (SumatraPDF.exe) with MSBuild and print only errors / warnings.
#   powershell -File tools/swiftleaf/build.ps1 [-Config Release|Debug] [-Target SumatraPDF]
param(
    [string]$Config = "Release",
    [string]$Target = "SumatraPDF"
)
$ErrorActionPreference = "Stop"
$root = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$msbuild = & $vswhere -products * -latest -requires Microsoft.Component.MSBuild -find "MSBuild\**\Bin\MSBuild.exe" | Select-Object -First 1
if (-not $msbuild) { throw "MSBuild not found; install Visual Studio 2022 (Build Tools) with the C++ workload" }

Push-Location $root
try {
    & $msbuild vs2022\SumatraPDF.sln "/t:$Target" "/p:Configuration=$Config;Platform=x64" /m /nologo /v:minimal "/clp:ErrorsOnly;WarningsOnly"
    if ($LASTEXITCODE -ne 0) { throw "build failed ($LASTEXITCODE)" }
    $outDir = if ($Config -eq "Release") { "out\rel64" } else { "out\dbg64" }
    Get-Item "$outDir\SumatraPDF.exe" | Select-Object FullName, Length, LastWriteTime
} finally {
    Pop-Location
}
