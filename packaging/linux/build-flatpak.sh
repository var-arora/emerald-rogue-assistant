#!/usr/bin/env bash

set -euo pipefail

if [[ $# -ne 1 || "$(uname -s)" != Linux || "$(uname -m)" != x86_64 ]]; then
  echo "usage: build-flatpak.sh OUTPUT_DIR (on Linux x86_64)" >&2
  exit 2
fi

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
repo_root="$(cd "$script_dir/../.." && pwd -P)"
mkdir -p "$1"
output_dir="$(cd "$1" && pwd -P)"
work="$repo_root/build/linux-flatpak"
stage="$work/app"
build="$work/build"
repository="$work/repository"
app_id=assistant.emerald.rogue
runtime_version=25.08
runtime_repo=https://dl.flathub.org/repo/flathub.flatpakrepo

flatpak remote-add --user --if-not-exists flathub "$runtime_repo"
flatpak install --user --noninteractive flathub \
  "org.freedesktop.Platform//$runtime_version" "org.freedesktop.Sdk//$runtime_version"

cmake -E rm -rf "$stage" "$build" "$repository"
mkdir -p "$work"
flatpak build-init "$stage" "$app_id" org.freedesktop.Sdk org.freedesktop.Platform "$runtime_version"

# Compile and test against the same libraries that the installed app will use.
build_options=(--filesystem="$repo_root" --share=network)
flatpak build "${build_options[@]}" "$stage" cmake -S "$repo_root" -B "$build" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/app \
  -DROGUE_BUILD_TESTS=ON -DROGUE_WARNINGS_AS_ERRORS=ON \
  -DROGUE_PACKAGE_PLATFORM=linux-x86_64 "-DROGUE_RELEASE_TAG=${ROGUE_RELEASE_TAG:-}"
flatpak build "${build_options[@]}" "$stage" cmake --build "$build" --parallel
flatpak build "${build_options[@]}" "$stage" ctest --test-dir "$build" --output-on-failure
flatpak build "${build_options[@]}" "$stage" cmake --install "$build" --component RogueAssistant --strip

version="$(sed -n '1p' "$build/RogueAssistant-build.txt")"
label="$(sed -n '2p' "$build/RogueAssistant-build.txt")"
if [[ ! "$label" =~ ^[0-9A-Za-z.-]+$ ]]; then
  echo "invalid package label" >&2
  exit 1
fi
flatpak build "${build_options[@]}" "$stage" cmake \
  -DROGUE_INSTALL_ROOT=/app -DROGUE_INSTALL_PLATFORM=linux \
  "-DROGUE_EXPECTED_VERSION=$version" -P "$repo_root/cmake/VerifyInstall.cmake"

flatpak build "$stage" appstreamcli compose --prefix=/ --origin="$app_id" \
  --result-root=/app --data-dir=/app/share/app-info/xmls \
  --icons-dir=/app/share/app-info/icons/flatpak --components="$app_id" /app
flatpak build-finish "$stage" --command=RogueAssistant \
  --share=network --share=ipc --socket=x11 --device=dri
flatpak build-export "$repository" "$stage" stable
flatpak build-bundle "$repository" "$output_dir/RogueAssistant-$label-linux-x86_64.flatpak" \
  "$app_id" stable --runtime-repo="$runtime_repo"
