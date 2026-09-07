param(
    [Parameter(Mandatory)][string]$Installer,
    [Parameter(Mandatory)][string]$Version
)

$ErrorActionPreference = "Stop"
if (-not $IsWindows -or $env:GITHUB_ACTIONS -ne "true" -or -not $env:RUNNER_TEMP) {
    throw "Run installer checks only on a disposable GitHub Windows runner."
}
$installerPath = (Resolve-Path -LiteralPath $Installer).Path
$repo = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$install = Join-Path $env:RUNNER_TEMP "Emerald Rogue Assistant install"
$userData = Join-Path $env:APPDATA "Emerald Rogue Assistant"
$shortcut = Join-Path ([Environment]::GetFolderPath("Programs")) "Emerald Rogue Assistant.lnk"
$registration = "HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\assistant.emerald.rogue_is1"
foreach ($path in @($install, $userData, $shortcut, $registration)) {
    if (Test-Path -LiteralPath $path) { throw "Refusing to replace existing test data: $path" }
}
New-Item -ItemType Directory -Path $userData | Out-Null
$savedData = Join-Path $userData "boxes.dat"
[IO.File]::WriteAllBytes($savedData, [byte[]](1, 2, 3, 4))
$savedHash = (Get-FileHash -LiteralPath $savedData).Hash

function Invoke-Installer {
    param([string]$Executable, [string[]]$Arguments)
    $process = Start-Process -FilePath $Executable -ArgumentList $Arguments -PassThru -Wait
    if ($process.ExitCode -ne 0) { throw "Installer exited with code $($process.ExitCode)." }
}

$installArguments = @("/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART", "/SP-", "/DIR=`"$install`"")
foreach ($attempt in 1..2) {
    Invoke-Installer $installerPath ($installArguments + "/LOG=`"$env:RUNNER_TEMP/install-$attempt.log`"")
    cmake "-DROGUE_INSTALL_ROOT=$install" -DROGUE_INSTALL_PLATFORM=windows `
        "-DROGUE_EXPECTED_VERSION=$Version" -P "$repo/cmake/VerifyInstall.cmake"
    if ($LASTEXITCODE -ne 0) { throw "Installed application checks failed." }
    $entry = Get-ItemProperty -LiteralPath $registration
    if ($entry.DisplayName -notlike "Emerald Rogue Assistant*" -or $entry.DisplayVersion -ne $Version) {
        throw "The uninstall entry has the wrong name or version."
    }
    if (-not (Test-Path -LiteralPath $shortcut)) { throw "The Start menu shortcut is missing." }
    $link = (New-Object -ComObject WScript.Shell).CreateShortcut($shortcut)
    if ($link.TargetPath -ne (Join-Path $install "RogueAssistant.exe")) {
        throw "The Start menu shortcut points to the wrong app."
    }
    if ((Get-FileHash -LiteralPath $savedData).Hash -ne $savedHash) { throw "User data changed." }
    if ($attempt -eq 1) {
        Remove-Item -LiteralPath (Join-Path $install "resources/RogueAssistant_mGBA.lua")
    }
}
Invoke-Installer (Join-Path $install "unins000.exe") @("/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART")
foreach ($path in @($install, $shortcut, $registration)) {
    if (Test-Path -LiteralPath $path) { throw "Uninstall left an application item: $path" }
}
if ((Get-FileHash -LiteralPath $savedData).Hash -ne $savedHash) { throw "Uninstall changed user data." }
Write-Output "Install, reinstall, Start menu shortcut, uninstall, and user data checks passed."
