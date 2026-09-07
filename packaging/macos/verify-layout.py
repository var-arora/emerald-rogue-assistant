"""Check the Finder layout saved inside a mounted installer image."""

import sys
from pathlib import Path

from ds_store import DSStore


def require(condition, message):
    if not condition:
        raise SystemExit(f"Cannot verify installer layout: {message}")


root = Path(sys.argv[1])
require((root / ".background.tiff").is_file(), "the drag arrow is missing")
with DSStore.open(str(root / ".DS_Store"), "r") as store:
    window = store["."]["bwsp"]
    icons = store["."]["icvp"]
    require(window["WindowBounds"] == "{{100, 100}, {640, 320}}", "wrong window size")
    for setting in ("ShowStatusBar", "ShowTabView", "ShowToolbar", "ShowPathbar", "ShowSidebar"):
        require(not window[setting], f"{setting} is enabled")
    require(icons["iconSize"] == 128, "wrong icon size")
    require(icons["arrangeBy"] == "none", "automatic icon sorting is enabled")
    require(icons["backgroundType"] == 2, "the drag arrow is not selected")
    require(store["RogueAssistant.app"]["Iloc"] == (160, 120), "wrong app position")
    require(store["Applications"]["Iloc"] == (480, 120), "wrong Applications position")
print("Installer window, icons, and drag arrow verified.")
