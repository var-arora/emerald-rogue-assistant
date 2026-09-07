# Bridge protocol 1.0

The mGBA Lua script and Emerald Rogue Assistant use one TCP connection. The app
is the server. It listens only on `127.0.0.1`, so both programs must run on the
same computer. This page defines the messages they exchange.

All integers use little-endian byte order: the least significant byte comes
first. Each message is sent as a frame, with a length, header, and payload
(the message data). Both programs keep incomplete data until a full frame is
ready. They also handle several frames in one receive and keep any bytes that
a partial send did not write.

## Frame format

```text
offset  size  field
0       4     body length, from 8 through 1,048,576
4       1     message type
5       1     flags, zero in version 1.0
6       2     reserved, zero
8       4     request ID
12      n     payload
```

The body length includes the eight-byte message header and the payload. It does
not include the four-byte length field.

A decoder rejects:

- An unknown message type
- Nonzero flags or reserved bytes
- A request ID that is not valid for the message
- A body that is too small or larger than 1 MiB
- A queue that is already full

The receive queue and send queue can each hold at most 256 frames and 4 MiB of
encoded messages. These limits put an upper bound on queue memory use. Neither
side sends a separate message to tell the other how much queue space is left.

A `ReadResult` can hold at most 1,048,568 memory bytes. A `WriteRequest` can
hold at most 1,048,560 memory bytes because its payload also has address and
size fields.

## Messages

`u8`, `u16`, and `u32` mean unsigned integers of 8, 16, and 32 bits.

| ID | Name | Request ID | Payload |
| ---: | --- | --- | --- |
| 1 | `ClientHello` | Zero | `RAB1`, protocol `u16 major, u16 minor`, script version `u32` |
| 2 | `ServerHello` | Zero | Status `u8`, reserved `u8`, protocol `u16, u16`, app version `u16, u16, u16` |
| 3 | `ReadRequest` | Nonzero | Address `u32`, byte count `u32` |
| 4 | `WriteRequest` | Nonzero | Address `u32`, byte count `u32`, bytes |
| 5 | `ReadResult` | Nonzero | Requested bytes |
| 6 | `WriteResult` | Nonzero | Empty |
| 7 | `Error` | Zero or source request | Error code `u16`, text length `u16`, UTF-8 text |
| 8 | `Close` | Zero | Empty |

Protocol 1.0 uses script version `1`. Error text can contain at most 1,024
bytes.

The error codes are:

| Code | Meaning |
| ---: | --- |
| 1 | Unsupported protocol |
| 2 | Bridge is busy |
| 3 | Bad frame |
| 4 | Bad request ID |
| 5 | Bad address |
| 6 | Bad size |
| 7 | Queue is full |
| 8 | Internal error |

## Connection setup

The script sends `ClientHello` first. The app accepts memory messages only
after the hello is complete.

Protocol major versions must match. A server can accept an older minor version
only when it supports every feature used by that version. This release accepts
only protocol 1.0.

The `ServerHello` contains the three numeric parts of the app version. Labels
such as `alpha.0`, `beta.1`, or a development commit ID are not sent in this
field. The full version still appears in the UI, log, and command output.

The app accepts one script. It sends a rejected hello, error code 2, and
`Close` to another script while the first one is connected. It sends `Close`
after any rejected hello.

The script reconnects no more than once per second. A script stop, ROM reset,
mGBA shutdown, app restart, or port change closes the old connection. The app
then waits for a new connection.

## Memory work in mGBA

During one emulated frame, the script:

- Receives at most 256 KiB and sends at most 256 KiB
- Starts or moves forward at most 64 memory operations
- Reads or writes at most 256 KiB of memory

A large operation continues in later frames.

Reads use `emu:readRange`. Writes use aligned `write32` and `write16` calls.
Leading or trailing bytes use `write8`.

The script checks addresses on its own. Reads can use the documented GBA RAM,
video memory, and ROM regions. Writes can use only EWRAM or IWRAM.

mGBA controls socket event checks. The script stores received bytes until a
full frame is ready. It also stores the unsent part reported by `socket:send`.
While mGBA is paused, frame callbacks do not run. Neither side closes the
bridge because it is idle. Queue limits keep pending work within a fixed bound.

## Shared test data

`tests/fixtures/bridge_protocol_1.golden` contains the standard byte examples.
The C++ tests and Lua tests both read this file. A change fails tests if the two
implementations no longer use the same bytes.
