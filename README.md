# Emerald Rogue Assistant

Emerald Rogue Assistant lets you use these Pokémon Emerald Rogue features
while playing in mGBA:

- Store Pokémon outside your game save with Home Box (Extended Storage).
- Connect to other players through the game's multiplayer features.

The app connects to mGBA through a Lua script, a small program that runs inside
the emulator. It reads and writes the running game's memory. It does not
include a ROM or change your ROM file.

This project adapts
[Pokabbie's original Windows app](https://github.com/Pokabbie/pokeemerald-rogue-assistant).
It builds for Windows, macOS, and Linux.

## Supported systems

- mGBA 0.10.5 or later
- Emerald Rogue 2.2.0 or later (Vanilla or EX)
- Windows x64
- macOS 11 or later on Apple silicon
- Linux x86_64

The app has been tested during play on Apple silicon with mGBA 0.10.5
and Emerald Rogue 2.2.0 Vanilla. Windows, Linux, EX, Home Box recovery, and
cross-platform multiplayer still need more live testing.

Both players must use this version of the assistant for multiplayer. It cannot
connect to the original Windows assistant. Home Box files keep the original
format and can be moved between the apps; see
[Move Home Box data](docs/installation.md#move-home-box-data).

## Get started

1. Download the package for your system from
   [GitHub Releases](https://github.com/avarun42/emerald-rogue-assistant/releases).
2. Follow the steps in [Install Emerald Rogue Assistant](docs/installation.md).
3. Start Emerald Rogue Assistant.
4. Open Emerald Rogue in mGBA.
5. Press `R` in Emerald Rogue Assistant to open its script folder.
6. In mGBA, select **Tools > Scripting**, then load
   `RogueAssistant_mGBA.lua` from that folder.

The app saves a copy of the script in its data folder each time it starts.
The script connects only to an assistant on the same computer. The default
connection port is `30125`; you normally do not need to change it.

If the app does not connect, see [Troubleshooting](docs/troubleshooting.md).

## Build from source

You need CMake 3.25 or later and a C++20 compiler.

```sh
cmake --preset dev-debug
cmake --build --preset dev-debug --parallel
ctest --preset dev-debug
```

See [Development](docs/development.md) for required software and build options.
See [Architecture](docs/architecture.md) for the main parts of the app.

## Technical documents

- [Bridge protocol](docs/bridge-protocol.md)
- [Home Box file format](docs/home-box-format.md)
- [Multiplayer protocol](docs/multiplayer-protocol.md)
- [Release process](docs/release.md)
- [Third-party notices](THIRD_PARTY_NOTICES.md)
