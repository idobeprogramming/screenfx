param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',
    [switch]$Package
)

$ErrorActionPreference = 'Stop'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) {
    throw 'Install Visual Studio with Desktop development with C++, CMake, and the Windows SDK.'
}
$installation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $installation) { throw 'No Visual Studio installation with the x64 C++ tools was found.' }
$developerPrompt = Join-Path $installation 'Common7\Tools\VsDevCmd.bat'
$preset = 'windows-' + $Configuration.ToLowerInvariant()
# The developer environment is scoped to the child shell; the user's PATH stays intact.
$buildCommand = 'call "{0}" -arch=x64 && cmake --preset {1} && cmake --build --preset {1} && ctest --preset {1} --output-on-failure' -f $developerPrompt, $preset
if ($Package) {
    $buildCommand += ' && cmake --install build/{0} --prefix dist' -f $Configuration.ToLowerInvariant()
}
Push-Location -LiteralPath $PSScriptRoot
try {
    & $env:ComSpec /d /s /c $buildCommand
    if ($LASTEXITCODE -ne 0) { throw "The build or tests failed (code $LASTEXITCODE)." }
    Write-Host ('Ready: ' + (Join-Path $PSScriptRoot ('build\{0}\ScreenFX.exe' -f $Configuration.ToLowerInvariant())))
} finally { Pop-Location }
