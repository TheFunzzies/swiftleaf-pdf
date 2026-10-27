# Run after bun cmd/build.ts -dbg. OpenSSL is optional.
param([string]$Tsa = 'https://timestamp.digicert.com')
$ErrorActionPreference = 'Stop'
Set-Location (Split-Path $PSScriptRoot -Parent)
New-Item -ItemType Directory -Force tmp | Out-Null
$vswhere = "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe"
$vs = & $vswhere -latest -version '[17,18)' -products '*' -property installationPath
$vcvars = Join-Path $vs 'VC/Auxiliary/Build/vcvars64.bat'
$libs = (Get-ChildItem out/dbg64/obj-s/*.lib | ForEach-Object { '"' + $_.FullName + '"' }) -join ' '
$compile = 'cl /nologo /MT /O1 /Gy /Iext/mupdf/include tests/signature-crypto.c /Fotmp/signature-crypto.obj /Fetmp/signature-crypto.exe /link /OPT:REF ' + $libs + ' crypt32.lib ncrypt.lib advapi32.lib user32.lib gdi32.lib shell32.lib ole32.lib comdlg32.lib ws2_32.lib windowscodecs.lib uuid.lib'
& $env:ComSpec /d /s /c "`"$vcvars`" >nul && $compile"
if ($LASTEXITCODE) { throw 'test compile failed' }
$cert = New-SelfSignedCertificate -Subject 'CN=SwiftLeaf signature regression' -CertStoreLocation Cert:\CurrentUser\My -Type Custom -KeySpec Signature -KeyUsage DigitalSignature
try {
    $password = ConvertTo-SecureString 'regression-only' -AsPlainText -Force
    Export-PfxCertificate -Cert $cert -FilePath "$PWD/tmp/signature-test.pfx" -Password $password | Out-Null
    & ./tmp/signature-crypto.exe tmp/signature-test.pfx regression-only none tmp/signature-plain.der
    if ($LASTEXITCODE) { throw 'plain signature failed' }
    & ./tmp/signature-crypto.exe tmp/signature-test.pfx regression-only http://127.0.0.1:1 tmp/signature-failed.der
    if (!$LASTEXITCODE) { throw 'unreachable TSA silently succeeded' }
    & ./tmp/signature-crypto.exe tmp/signature-test.pfx regression-only $Tsa tmp/signature-stamped.der
    if ($LASTEXITCODE) { throw 'timestamp signature failed' }
    $openssl = Get-Command openssl -ErrorAction SilentlyContinue
    if (!$openssl -and (Test-Path 'C:/Program Files/Git/usr/bin/openssl.exe')) { $openssl = @{ Source = 'C:/Program Files/Git/usr/bin/openssl.exe' } }
    if ($openssl) {
        [IO.File]::WriteAllText("$PWD/tmp/signature-data.txt", "signature regression`n", [Text.UTF8Encoding]::new($false))
        & $openssl.Source cms -verify -inform DER -in tmp/signature-stamped.der -content tmp/signature-data.txt -noverify -out tmp/signature-verified.txt
        if ($LASTEXITCODE) { throw 'OpenSSL CMS verification failed' }
        & $openssl.Source cms -verify -inform DER -in tmp/signature-token.der -noverify -out tmp/signature-tstinfo.der
        if ($LASTEXITCODE) { throw 'OpenSSL TSA signature verification failed' }
    }
} finally {
    Remove-Item -LiteralPath "Cert:\CurrentUser\My\$($cert.Thumbprint)"
    Remove-Item -LiteralPath tmp/signature-test.pfx -ErrorAction SilentlyContinue
}

