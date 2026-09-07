#!/usr/bin/env bash

set -euo pipefail

if [[ $# -ne 3 ]]; then
  echo "usage: build-appimage.sh BUILD_DIR OUTPUT_DIR LINUXDEPLOY" >&2
  exit 2
fi

build_dir="$1"
output_dir="$2"
linuxdeploy="$3"
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
repo_root="$(cd "$script_dir/../.." && pwd -P)"

if [[ ! -d "$build_dir" ]]; then
  echo "build directory does not exist: $build_dir" >&2
  exit 2
fi
if [[ ! -x "$linuxdeploy" ]]; then
  echo "linuxdeploy is not executable: $linuxdeploy" >&2
  exit 2
fi
if [[ "$(uname -s)" != Linux || "$(uname -m)" != x86_64 ]]; then
  echo "AppImage packaging requires Linux x86_64" >&2
  exit 2
fi

build_dir="$(cd "$build_dir" && pwd -P)"
version="$(sed -n '1p' "$build_dir/RogueAssistant-build.txt")"
build_label="$(sed -n '2p' "$build_dir/RogueAssistant-build.txt")"
if [[ ! "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+(-[0-9A-Za-z-]+(\.[0-9A-Za-z-]+)*)?$ ||
      ! "$build_label" =~ ^[0-9A-Za-z.-]+$ ]]; then
  echo "invalid build version; configure and build the app again" >&2
  exit 2
fi
mkdir -p "$output_dir"
output_dir="$(cd "$output_dir" && pwd -P)"

stage_root="$build_dir/appimage"
app_dir="$stage_root/AppDir"
case "$app_dir" in
  "$build_dir"/appimage/AppDir) ;;
  *)
    echo "refusing unsafe AppDir path: $app_dir" >&2
    exit 2
    ;;
esac

cmake -E rm -rf "$stage_root"
cmake -E make_directory "$app_dir"
DESTDIR="$app_dir" cmake --install "$build_dir" --prefix /usr --component RogueAssistant --strip
cmake \
  "-DROGUE_INSTALL_ROOT=$app_dir/usr" \
  -DROGUE_INSTALL_PLATFORM=linux \
  "-DROGUE_EXPECTED_VERSION=$version" \
  -P "$repo_root/cmake/VerifyInstall.cmake"

executable="$app_dir/usr/bin/RogueAssistant"
desktop_file="$app_dir/usr/share/applications/assistant.emerald.rogue.desktop"
icon_file="$app_dir/usr/share/icons/hicolor/128x128/apps/assistant.emerald.rogue.png"
for required_file in "$executable" "$desktop_file" "$icon_file"; do
  if [[ ! -f "$required_file" ]]; then
    echo "installed AppDir is missing: $required_file" >&2
    exit 1
  fi
done

output="$output_dir/RogueAssistant-$build_label-linux-x86_64.AppImage"
cmake -E rm -f "$output"

export ARCH=x86_64
export OUTPUT="$output"
export VERSION="$version"
"$linuxdeploy" --appimage-extract-and-run \
  --appdir "$app_dir" \
  --executable "$executable" \
  --desktop-file "$desktop_file" \
  --icon-file "$icon_file" \
  --output appimage

if [[ ! -x "$output" ]]; then
  echo "linuxdeploy did not create the expected AppImage: $output" >&2
  exit 1
fi

"$output" --appimage-extract-and-run --version

verify_root="$stage_root/extracted"
cmake -E rm -rf "$verify_root"
cmake -E make_directory "$verify_root"
(
  cd "$verify_root"
  "$output" --appimage-extract >/dev/null
)
cmake \
  "-DROGUE_INSTALL_ROOT=$verify_root/squashfs-root/usr" \
  -DROGUE_INSTALL_PLATFORM=linux \
  "-DROGUE_EXPECTED_VERSION=$version" \
  -P "$repo_root/cmake/VerifyInstall.cmake"
cmake -E rm -rf "$verify_root"
