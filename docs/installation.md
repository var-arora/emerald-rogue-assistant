# Install Emerald Rogue Assistant

Emerald Rogue Assistant is separate from mGBA and Pokémon Emerald Rogue. Release
packages do not include a ROM.

## Before you install

You need:

- mGBA 0.10.5 or later with Lua support
- Emerald Rogue 2.2.0 or later (Vanilla or EX)
- Windows x64, macOS 11 or later on Apple silicon, or Linux x86_64

The assistant connects to mGBA on port `30125`. No other app can use that port
at the same time. Multiplayer uses a separate port, `30025` by default. The
host may need to allow the multiplayer port through a firewall.

Follow the steps for [Windows](#windows), [macOS](#macos), or [Linux](#linux),
then [connect mGBA](#connect-mgba).

## Check the download

Each release includes `SHA256SUMS`, a list of checksums. A checksum is a value
calculated from a file's contents. Use it to check that your download matches
the published file.

In the commands below, replace `<build>` with the label in your downloaded
filename. Development builds use `dev-` followed by a commit ID. Releases use
a version such as `1.0.0-alpha.0`.

On Windows PowerShell:

```powershell
Get-FileHash '.\RogueAssistant-<build>-windows-x64.exe' -Algorithm SHA256
```

On macOS:

```sh
shasum -a 256 'RogueAssistant-<build>-macos-arm64.dmg'
```

On Linux:

```sh
sha256sum 'RogueAssistant-<build>-linux-x86_64.flatpak'
```

Compare the full value with the matching line in `SHA256SUMS`. Do not use the
file if the values differ.

## Windows

Open `RogueAssistant-<build>-windows-x64.exe` and follow the setup steps. Then
open **Emerald Rogue Assistant** from the Start menu. If you downloaded a
development build from GitHub Actions, first extract its ZIP to get the installer.

Setup installs the app for your Windows account without asking for
administrator access. The default folder is
`%LOCALAPPDATA%\Programs\Emerald Rogue Assistant`.

To update the app, close it and run the new installer. Leave Extended Storage
in the game and finish any transfer before closing the assistant. Setup keeps
your settings and Home Box files.

These Windows builds do not have a verified publisher signature. Windows may
show a security warning. Check that you downloaded the installer from this
repository before choosing to run it.

## macOS

Open `RogueAssistant-<build>-macos-arm64.dmg`. Drag
`RogueAssistant.app` to Applications.

The DMG opens a small window with the app on the left and Applications on the
right. Drag the app to Applications. If you downloaded a
development build from GitHub Actions, first extract its ZIP and open the DMG
inside. Downloads from GitHub Releases provide the DMG directly.

The macOS app runs only on Apple silicon. Builds without an Apple Developer ID
signature can show an unknown developer warning.

If macOS blocks the app:

1. Control-click `RogueAssistant.app` in Finder.
2. Select **Open**.
3. Select **Open** again in the warning.

If **Open** is not available, try to open the app once. Then go to **System
Settings > Privacy & Security** and select **Open Anyway**.

## Linux

Download `RogueAssistant-<build>-linux-x86_64.flatpak`. Open it in your software
manager and select **Install**. On SteamOS, switch to Desktop Mode first and
open the file with **Discover**. If you downloaded a development build from
GitHub Actions, first extract its ZIP to get the `.flatpak` file.

After installation, open **Emerald Rogue Assistant** from the desktop's
application menu. The app no longer depends on the downloaded installer;
you can delete that file.

Flatpak must be available on your system. SteamOS includes it. On other Linux
systems, follow the [Flatpak setup guide](https://flatpak.org/setup/).
Installation needs an internet connection to download shared system libraries
if they are not already installed. The package is not an offline installer.

If your file manager does not open the installer, use:

```sh
flatpak install --user 'RogueAssistant-<build>-linux-x86_64.flatpak'
flatpak run assistant.emerald.rogue
```

To update, close the app and install the new `.flatpak` file. Your settings
and Home Box data are kept. This direct download does not provide automatic
app updates.

On SteamOS, use the assistant and mGBA in Desktop Mode. Installation does not
add either app to Steam Gaming Mode. Connect mGBA using the steps below.

## Connect mGBA

1. Start Emerald Rogue Assistant. It saves a copy of its Lua script and waits for mGBA.
2. Open Emerald Rogue in mGBA.
3. Press `R` in Emerald Rogue Assistant to open the exported script folder.
4. In mGBA, select **Tools > Scripting**.
5. Select **File > Load Script**.
6. Open `RogueAssistant_mGBA.lua` from the exported script folder.
7. Wait for Emerald Rogue Assistant to show that mGBA is connected.

The waiting screen has these controls:

- `P`: change the saved connection port
- `E`: export the script again
- `C`: copy the script path
- `R`: open the script folder

The script contains the port number used when it was exported. After changing
the port, export the script again and load that copy in mGBA. Also reload the
script after installing an assistant update. While the script is loaded, it
reconnects after an app restart or a ROM reset.

## Command line

```text
RogueAssistant [--bridge-port PORT] [--version] [--help]
```

`--bridge-port` changes the port for that run only. It does not change
`settings.ini`.

The app chooses the bridge port in this order:

1. The `--bridge-port` value
2. `Bridge.Port` in `settings.ini`
3. Port `30125`

## User files

The app stores settings, logs, its script, and Home Box data in these folders:

- Windows: `%APPDATA%\Emerald Rogue Assistant`
- macOS: `~/Library/Application Support/assistant.emerald.rogue`
- Linux Flatpak data: `~/.var/app/assistant.emerald.rogue/data/emerald-rogue-assistant`
- Linux Flatpak settings: `~/.var/app/assistant.emerald.rogue/config/emerald-rogue-assistant`

A Linux build run outside Flatpak uses `$XDG_DATA_HOME/emerald-rogue-assistant`
for data and `$XDG_CONFIG_HOME/emerald-rogue-assistant` for settings. If those
variables are unset or are not absolute paths, it uses `~/.local/share` and
`~/.config` as the base folders.

The log is `logs/RogueAssistant.log` in the data folder. The Lua script is in
the `scripts` folder. Home Box files are stored by ROM edition and trainer ID.

`settings.ini` can contain these keys:

```ini
Multiplayer.HostPort=30025
Multiplayer.JoinIP=
Bridge.Port=30125
```

On Windows, the first run can copy data from the original assistant. It checks
`%APPDATA%\.pokabbie\rogue_assistant`. It also looks for `settings.ini` in the
folder from which you start this app. It copies these files only when the new
data folder does not exist. It does not remove the old files.

## Remove the app

On Windows, open **Settings > Apps**, select **Emerald Rogue Assistant**, and
choose **Uninstall**. On macOS, move the app from Applications to the Trash.
On Linux, remove the app through your software manager. Keep its user data
when asked if you want to keep your Home Box files.

User files remain in the folders listed above. Back up any Home Box files
that you want to keep before you delete those folders.

## Move Home Box data

This app reads and writes the same Home Box file format as the original
Windows assistant. It does not convert your files to a new format.

The apps use separate data folders. The Windows import copies existing files
once. Later changes in one folder do not appear in the other.

To move data between apps or computers:

1. Finish any storage transfer and leave Extended Storage in the game.
2. Close both assistant apps.
3. Back up the game save and both assistant data folders.
4. Copy the latest `<edition>/<trainer ID>/boxes.dat` to the same relative
   path in the destination app's data folder. Keep the edition and trainer
   folders unchanged.

The original Windows app's data folder is
`%APPDATA%\.pokabbie\rogue_assistant`. This app's folders are listed under
[User files](#user-files). Use the matching game save and ROM edition.
