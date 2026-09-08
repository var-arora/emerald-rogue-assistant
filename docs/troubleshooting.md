# Troubleshooting

## Emerald Rogue Assistant keeps waiting for mGBA

Check these items:

1. Use mGBA 0.10.5 or later.
2. Start Emerald Rogue 2.2.0 or later, in either the Vanilla or EX edition.
3. Press `R` in Emerald Rogue Assistant to open the exported script folder.
4. Open **Tools > Scripting** in mGBA.
5. Load `RogueAssistant_mGBA.lua` from the exported script folder.
6. Make sure only one Emerald Rogue Assistant app uses that port and only one mGBA
   script is connected.

If you changed the port, press `E` in Emerald Rogue Assistant to export the
script again. Load that copy in mGBA. Reload the script after an assistant
update too; mGBA may still be running the old copy.

## The port is already in use

If the app reports "Cannot listen on port", close any other copies of Emerald
Rogue Assistant. Then press `P` in the waiting window and press Enter to retry
the same port. You do not need to restart the assistant.

If another program needs that port, press `P`, enter a different port from 1
to 65535, then press Enter. The app keeps a working connection port if it
cannot open the new one.

The app exports its Lua script after it opens the port. If you changed the
port, load the updated script in mGBA. You can also set the port when starting
the app; see [Command line](installation.md#command-line).

## The ROM is not compatible

Use Emerald Rogue 2.2.0 or later, in either the Vanilla or EX edition. If you
have just updated the game, check for an update to Emerald Rogue Assistant too.

## Storage transfer stopped

If the message also says "Home Box file was not changed", the connection ended
before mGBA confirmed all Pokémon writes. The assistant kept the last saved
Home Box file and backup. Do not save the game yet: some game memory may have
changed before the connection ended. Keep your game save and Home Box files,
and check your Pokémon before trying another transfer.

If the app asks you to reopen Extended Storage, it reconnected while that
screen was already open. Press B, then open Extended Storage again. This lets
the game start a new storage connection.

## Home Box could not load or save

Do not delete the Home Box files. If the main file is damaged, the assistant
stops storage access so it cannot accept a transfer that it cannot save.

Copy the complete trainer folder before you try to repair anything. See
[Home Box file format](home-box-format.md) for file names and recovery rules.

## macOS blocks the app

An app without an Apple Developer ID signature can show an unknown developer
warning. Follow the [macOS installation steps](installation.md#macos).

The macOS app requires Apple silicon. It does not run on an Intel Mac.

## Find the log

The log file is named `RogueAssistant.log`:

- Windows: `%APPDATA%\Emerald Rogue Assistant\logs`
- macOS: `~/Library/Application Support/assistant.emerald.rogue/logs`
- Linux Flatpak: `~/.var/app/assistant.emerald.rogue/data/emerald-rogue-assistant/logs`

If you built the Linux app from source and run it outside Flatpak, see
[User files](installation.md#user-files) for its data folder. The log is in
the `logs` folder there.

When you report a problem, include:

- Your operating system and CPU type
- The versions of Emerald Rogue Assistant, mGBA, and Emerald Rogue
- The Vanilla or EX edition
- What you expected
- What happened
- The relevant log lines, after you check them for private data
