# Multiplayer protocol 1.0

Emerald Rogue Assistant uses the ENet network library to send multiplayer
data. It copies the game's setup messages, game state, player profiles, and
player state as byte blocks. It checks these blocks against the layout and
sizes given by the ROM. It does not recreate the game's multiplayer logic.

Before game data can move, both apps exchange a compatibility message. The
original Windows assistant does not send this message, so it cannot connect to
version 1.0.

## ENet channels

| Channel | Data | Delivery |
| ---: | --- | --- |
| 0 | Compatibility message | Reliable |
| 1 | ROM handshake | Reliable |
| 2 | Host game state | Reliable |
| 3 | Player state | Unreliable fragment |
| 4 | Player profiles | Reliable |

Reliable delivery retries lost packets. Unreliable fragments allow a message
to be split into packets without retrying those that are lost. The ROM
handshake is the exchange of setup messages between the games. A peer is
another connected assistant.

The app does not process game data until the sender passes the compatibility
check. The host sends game data only to a peer that also finished the ROM
handshake.

## Compatibility message

The first channel 0 message is exactly 40 bytes. All integers use unsigned
little-endian byte order, with the least significant byte first. The ROM
Assistant API is the version of the interface that the game provides to the
app.

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 4 | ASCII magic `RAMP` |
| 4 | 2 | Protocol major, `1` |
| 6 | 2 | Protocol minor, `0` |
| 8 | 4 | ROM Assistant API, exactly `3` |
| 12 | 1 | ROM edition: Vanilla `0`, EX `1` |
| 13 | 3 | Reserved, zero |
| 16 | 4 | ROM player count |
| 20 | 4 | Full multiplayer state size |
| 24 | 4 | ROM handshake size |
| 28 | 4 | Host game state size |
| 32 | 4 | Player profile size |
| 36 | 4 | Player state size |

Both peers must have the same:

- Protocol major
- ROM Assistant API
- ROM edition
- Player count
- Five structure sizes

The peers choose the lower minor version when both sides support it. Protocol
1.0 has no optional minor features.

A peer has five seconds after the ENet connection starts to send one valid
compatibility message.

## Invalid peers

Emerald Rogue Assistant disconnects a peer that sends:

- A message with a bad size or field value
- More than one compatibility message
- Values that do not match the local game
- Game data before the compatibility check or ROM handshake, except for the
  early handshake described below
- A message on the wrong channel or from the wrong side
- A player ID outside the ROM player table
- A payload larger than the matching ROM structure

The local app also shows an error.

ENet orders reliable messages within one channel, but it does not order one
channel against another. For this reason, the app can hold one valid early ROM
handshake until the compatibility message arrives.
