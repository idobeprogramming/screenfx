param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',
    [switch]$Package
)

$ErrorActionPreference = 'Stop'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) {
    throw 'Installez Visual Studio avec Développement Desktop en C++, CMake et le SDK Windows.'
}
$installation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $installation) { throw 'Aucune installation Visual Studio avec les outils C++ x64 détectée.' }
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
    if ($LASTEXITCODE -ne 0) { throw "La compilation ou les tests ont échoué (code $LASTEXITCODE)." }
    Write-Host ('Prêt : ' + (Join-Path $PSScriptRoot ('build\{0}\ScreenFX.exe' -f $Configuration.ToLowerInvariant())))
} finally { Pop-Location }
