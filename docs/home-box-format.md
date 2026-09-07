# Home Box file format

Emerald Rogue Assistant reads and writes the original Windows assistant's
format, version 0. No conversion is needed when moving files between the apps.

Files are stored at `<data folder>/<edition>/<trainer ID>/boxes.dat`. The
edition and trainer ID come from the folder path, not the file contents. Keep
that path when copying a file. The ROM API is not stored in the file either.

All integers are unsigned and use little-endian byte order, with the least
significant byte first. Pokémon data and box metadata (such as the box name)
are copied as bytes without changing the game's data.

## Header

The header contains five 32-bit integers:

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 4 | Header marker, `3497814` |
| 4 | 4 | File format, `0` |
| 8 | 4 | Number of stored boxes |
| 12 | 4 | Metadata bytes per box |
| 16 | 4 | Pokémon bytes per box |

The box count and byte sizes must match the running game's layout.

## Box records and footer

Each stored box has one record. Records appear in box order and contain:

| Size | Field |
| --- | --- |
| Metadata bytes per box, from the header | Metadata bytes |
| Pokémon bytes per box, from the header | Pokémon bytes |
| 4 | Sum of all metadata and Pokémon bytes in this record |

The checksum is an unsigned 32-bit sum of the record's data bytes. The reader
uses it to check for file damage. It is the same checksum used by the original
assistant. It detects some changes to the bytes, but not all changes.
There is no stored box index or whole-file checksum.

The final four bytes are the footer marker, `7893612`.

The reader rejects a wrong marker, unknown version, wrong box count or byte
size, incorrect checksum, incomplete file, or extra bytes after the footer.
It also limits files to 64 MiB and box counts to 255 before reserving memory
for the data.

## Save and recovery

The app writes a temporary file beside `boxes.dat`, then replaces `boxes.dat`
with an atomic rename: readers see either the old file or the new file, not
a partly written file. It keeps the previous valid file as `boxes.dat.bak`.
These checks and save steps do not change the file format.

The app waits for mGBA to confirm each Pokémon write before saving the new
box order. If a disconnect interrupts the transfer, the app keeps the last
saved file and backup unchanged and reports that the transfer stopped. Some
game memory may already have changed; the game save and Home Box file are not
saved together in one step.

If `boxes.dat` is missing, the app can load `boxes.dat.bak` and shows a warning.
If `boxes.dat` is damaged, the app stops the connection before enabling storage
transfers. It does not overwrite the damaged file, even if the backup is valid.
This prevents a transfer that the app cannot save.

To recover a damaged main file:

1. Leave Extended Storage in the game and close the assistant.
2. Copy the complete trainer folder to a safe place.
3. Keep the damaged file by renaming it to `boxes.dat.damaged`.
4. If `boxes.dat.bak` is available, copy it to `boxes.dat`. Keep the backup too.
5. Restart the assistant and reload its script in mGBA. Check the stored Pokémon
   before making another transfer.

The backup may not contain the most recent transfer. Keep your game saves and
the copied trainer folder until you have checked the result. If no valid backup
is available, keep the files and report the problem instead of replacing them
with empty storage.

The original assistant uses a different data folder. See
[Move Home Box data](installation.md#move-home-box-data) before switching apps.
