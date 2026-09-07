param(
    [Parameter(Mandatory)][string]$BuildDirectory,
    [Parameter(Mandatory)][string]$OutputDirectory
)

$ErrorActionPreference = "Stop"
if (-not $IsWindows) { throw "Windows packaging must run on Windows." }
$build = (Resolve-Path -LiteralPath $BuildDirectory).Path
$output = [IO.Path]::GetFullPath($OutputDirectory)
$repo = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$versionLines = Get-Content -LiteralPath (Join-Path $build "RogueAssistant-build.txt")
$version = $versionLines[0]
$label = $versionLines[1]
if ($version -notmatch '^(\d+\.\d+\.\d+)(-[0-9A-Za-z.-]+)?$' -or
    $label -notmatch '^[0-9A-Za-z.-]+$') {
    throw "Invalid build version. Configure and build the app again."
}
$core = ($version -split '-', 2)[0]
$compiler = Join-Path ${env:ProgramFiles(x86)} "Inno Setup 6\ISCC.exe"
if (-not (Test-Path -LiteralPath $compiler -PathType Leaf)) {
    throw "Install Inno Setup 6 before building the Windows installer."
}
$stage = Join-Path $build "windows-package"
if (Test-Path -LiteralPath $stage) { Remove-Item -LiteralPath $stage -Recurse -Force }
New-Item -ItemType Directory -Path $stage, $output -Force | Out-Null
cmake --install $build --prefix $stage --component RogueAssistant
if ($LASTEXITCODE -ne 0) { throw "Cannot stage the Windows app." }
cmake "-DROGUE_INSTALL_ROOT=$stage" -DROGUE_INSTALL_PLATFORM=windows `
    "-DROGUE_EXPECTED_VERSION=$version" -P "$repo/cmake/VerifyInstall.cmake"
if ($LASTEXITCODE -ne 0) { throw "Staged application checks failed." }
& $compiler "/DAppVersion=$version" "/DVersionCore=$core" "/DBuildLabel=$label" `
    "/DStageDir=$stage" "/DOutputPath=$output" "$PSScriptRoot/installer.iss"
if ($LASTEXITCODE -ne 0) { throw "Cannot build the Windows installer." }
$installer = Join-Path $output "RogueAssistant-$label-windows-x64.exe"
if (-not (Test-Path -LiteralPath $installer -PathType Leaf)) {
    throw "The Windows installer was not created."
}
Write-Output $installer
