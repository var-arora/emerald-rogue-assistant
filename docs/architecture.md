# Architecture

Emerald Rogue Assistant is one desktop app and one mGBA Lua script. The app
contains all game, Home Box, multiplayer, storage, and user interface logic.
The Lua script reads and writes game memory when the app asks it to, then
returns the results. It does not decide how game features work.

## Main parts

| CMake target | Purpose |
| --- | --- |
| `rogue_core` | Common data types, files, settings, logs, and ROM data |
| `rogue_bridge` | Memory requests and the local TCP bridge |
| `rogue_multiplayer` | ENet network code and multiplayer checks |
| `rogue_app` | Game sessions, Home Box, and app state |
| `RogueAssistant` | SFML window and process entry point |
| `rogue_tests` | Unit and behavior tests |

## Data flow

```text
mGBA and Emerald Rogue                  Emerald Rogue Assistant
----------------------                 -----------------------
Lua frame callback                     main thread: window and input
readRange and write8/16/32      TCP     worker thread: game session
limited work per frame         <----->  Home Box, files, and multiplayer
```

The bridge is the local TCP connection between the script and the app. It
listens only on `127.0.0.1`, so both programs must run on the same computer.
It accepts one mGBA script at a time.
The script reconnects when mGBA resets or the app restarts.

## ROM data

Emerald Rogue Assistant does not parse or patch the ROM file. It reads the
running game's memory.

The app first reads the ROM information at `0x08000100`. It checks the Emerald
Rogue markers, then follows a pointer to the Rogue Assistant header. That
header tells the app where to find game data and how it is laid out: addresses,
sizes, offsets, box count, and player count.

The ROM Assistant API is the interface that the game provides to the app.
The app requires version 3. It also checks the ROM edition: `0` means Vanilla
and `1` means EX. It checks addresses, sizes, and array indexes from the ROM
before using them. `tests/CharacterizationTests.cpp` and
`tests/GameBehaviourTests.cpp` cover these layout checks.

## Memory requests

`GameSession` gives each read or write a nonzero 32-bit request ID. It keeps the
matching callback, a function to run when the bridge returns a result. Only the
worker thread runs these callbacks.

The request rules are:

- No more than 256 requests can be waiting.
- A message body, including its header, can contain at most 1 MiB. See the
  [bridge protocol](bridge-protocol.md#frame-format) for the data limits.
- Reads can use EWRAM, IWRAM, palette RAM, VRAM, OAM, or cartridge ROM.
- Writes can use only EWRAM or IWRAM.
- A request cannot be empty, cross a memory region, or overflow an address.
- A read result must have the requested size.
- A write result must have no data.

EWRAM and IWRAM are the game's main RAM regions. Palette RAM, VRAM, and OAM
hold graphics data.

When the bridge is full, the app waits for room before asking for more game
memory. This keeps pending work within the limits even when mGBA is paused.

`TcpLuaTransport` owns the listener and socket. Its socket calls are
nonblocking: they return without waiting for a connection, new data, or room
to send. The worker thread handles new connections, reads, messages, and
partial sends. There is no separate network thread or time limit for an idle
bridge connection.

## Thread ownership

The process main thread creates the SFML window, reads input, and draws it
when the visible content changes. One `SessionWorker` thread owns all changing
game state, network state, and file work.

The UI sends plain `UiCommand` values to the worker. The worker gives the UI a
`UiSnapshot`, a copy of the current state to show. UI code cannot read or
change a game behavior object.

At most 256 UI commands can wait. Shutdown follows this order:

1. Stops accepting commands.
2. Stops the bridge.
3. Disconnects the game.
4. Detaches each behavior, finishes any active save, and closes multiplayer.
5. Saves settings.
6. Publishes its last UI state.
7. Waits for the worker thread to finish before destroying the window.

The main thread handles the last step. The worker also tries to shut down
after an exception and puts the error in its last UI state.

## Files and resources

Platform code chooses the data, settings, and resource folders. Resources are
the images, fonts, and Lua script that ship with the app. Feature code uses
these paths and does not build its own system-specific path.

The app uses `std::filesystem::path` for paths and UTF-8 for text. It writes
settings and Home Box data to temporary files first. It then uses an atomic
rename, which replaces the old file in one step. Readers see either the old
file or the new file, not a partly written file.

On macOS, resources are in `RogueAssistant.app/Contents/Resources`. On Windows
and Linux, the `resources` folder is beside the app. User paths are listed in
[Install Emerald Rogue Assistant](installation.md#user-files).

## Original Windows design

The original app wrote a Lua script that loaded `RogueAssistant.dll` inside
mGBA. The DLL created the SFML window and used shared queues to reach mGBA's
Lua thread.

The current app no longer builds or loads that DLL. The local TCP bridge now
uses the same design on every supported system. The existing C++ game logic
uses memory requests through `IGameMemoryTransport`.
