"""Start the installed app and check its bridge from outside the sandbox."""

import os
import socket
import struct
import subprocess
import time

if os.environ.get("GITHUB_ACTIONS") != "true":
    raise SystemExit("Run this check only on a disposable GitHub runner.")

app_id = "assistant.emerald.rogue"
process = subprocess.Popen(["flatpak", "run", app_id, "--bridge-port", "30126"])


def receive_exact(peer, size):
    result = b""
    while len(result) < size:
        chunk = peer.recv(size - len(result))
        if not chunk:
            raise RuntimeError("Bridge disconnected before its reply.")
        result += chunk
    return result


try:
    deadline = time.monotonic() + 15
    while True:
        if process.poll() is not None:
            raise RuntimeError("The installed app exited during startup.")
        try:
            peer = socket.create_connection(("127.0.0.1", 30126), timeout=1)
            break
        except OSError:
            if time.monotonic() >= deadline:
                raise RuntimeError("The installed app did not open its bridge.")
            time.sleep(0.1)
    with peer:
        peer.sendall(bytes.fromhex("140000000100000000000000524142310100000001000000"))
        length = struct.unpack("<I", receive_exact(peer, 4))[0]
        if not 10 <= length <= 1024:
            raise RuntimeError("Invalid bridge reply size.")
        reply = receive_exact(peer, length)
        if reply[0] != 2 or reply[8:10] != b"\x00\x00":
            raise RuntimeError("The bridge rejected the connection.")
        peer.sendall(bytes.fromhex("080000000800000000000000"))
finally:
    subprocess.run(["flatpak", "kill", app_id], check=False)
    process.wait(timeout=10)

print("Installed app opened its window and accepted the bridge connection.")
