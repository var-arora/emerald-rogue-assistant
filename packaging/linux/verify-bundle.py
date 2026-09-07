"""Check the app details that a software manager reads from the installer."""

import gzip
import sys
import xml.etree.ElementTree as ET

import gi

gi.require_version("Flatpak", "1.0")
from gi.repository import Flatpak, Gio

bundle = Flatpak.BundleRef.new(Gio.File.new_for_path(sys.argv[1]))
appstream = bundle.get_appstream()
if appstream is None:
    raise SystemExit("The installer is missing its app details.")
root = ET.fromstring(gzip.decompress(appstream.get_data()))
component = root.find("component")
if component is None or component.findtext("id") != "assistant.emerald.rogue":
    raise SystemExit("The installer has the wrong app ID.")
if component.findtext("name") != "Emerald Rogue Assistant":
    raise SystemExit("The installer has the wrong app name.")
release = component.find("releases/release")
if release is None or release.get("version") != sys.argv[2]:
    raise SystemExit("The installer has the wrong app version.")
if bundle.get_icon(128) is None:
    raise SystemExit("The installer is missing its app icon.")
print("Installer app name, version, and icon verified.")
