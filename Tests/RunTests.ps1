# Builds and runs the ProjectFs and highlighter unit checks (in a scratch folder under %TEMP%).
param([string]$Work = (Join-Path $env:TEMP "ProjectBrowserTests"))
$ErrorActionPreference = "Continue"
$tests = Split-Path -Parent $MyInvocation.MyCommand.Path
$plugin = Split-Path -Parent $tests
if (Test-Path -LiteralPath $Work) { Remove-Item -LiteralPath $Work -Recurse -Force }
New-Item -ItemType Directory -Force -Path $Work | Out-Null
$installer = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer"
$env:PATH = "$installer;$env:PATH"
$vs = & (Join-Path $installer "vswhere.exe") -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$vs) { throw "Visual Studio with the C++ tools was not found." }
$vcvars = Join-Path $vs "VC\Auxiliary\Build\vcvars64.bat"
$exe = Join-Path $Work "ProjectTests.exe"
$sources = @("Tests\ProjectTests.cpp", "ProjectFs.cpp", "Highlighter.cpp") | ForEach-Object { "`"$(Join-Path $plugin $_)`"" }
cmd /c "`"$vcvars`" >nul && cl /nologo /std:c++17 /O2 /EHsc /W3 /utf-8 /DNOMINMAX /DWIN32_LEAN_AND_MEAN /DUNICODE /D_UNICODE /Fe`"$exe`" /Fo`"$Work\\`" $($sources -join ' ') shell32.lib"
if ($LASTEXITCODE -ne 0) { throw "Test build failed." }
& $exe (Join-Path $Work "data")
if ($LASTEXITCODE -ne 0) { throw "Unit checks failed." }
