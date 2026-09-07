#!/usr/bin/env bash

set -euo pipefail

if [[ $# -ne 2 ]]; then
  echo "usage: package.sh BUILD_DIR OUTPUT_DIR" >&2
  exit 2
fi

build_dir="$1"
output_dir="$2"
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
repo_root="$(cd "$script_dir/../.." && pwd -P)"

if [[ ! -d "$build_dir" ]]; then
  echo "build directory does not exist: $build_dir" >&2
  exit 2
fi
if [[ "$(uname -s)" != Darwin ]]; then
  echo "macOS packaging must run on macOS" >&2
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

stage_root="$build_dir/macos-package"
case "$stage_root" in
  "$build_dir"/macos-package) ;;
  *)
    echo "refusing unsafe staging path: $stage_root" >&2
    exit 2
    ;;
esac

cmake -E rm -rf "$stage_root"
cmake -E make_directory "$stage_root"
cmake --install "$build_dir" --prefix "$stage_root" --component RogueAssistant --strip
cmake \
  "-DROGUE_INSTALL_ROOT=$stage_root" \
  -DROGUE_INSTALL_PLATFORM=macos \
  "-DROGUE_EXPECTED_VERSION=$version" \
  -P "$repo_root/cmake/VerifyInstall.cmake"

app="$stage_root/RogueAssistant.app"
executable="$app/Contents/MacOS/RogueAssistant"
if [[ ! -x "$executable" ]]; then
  echo "installed application bundle is missing its executable" >&2
  exit 1
fi
if [[ "$(lipo -archs "$executable")" != arm64 ]]; then
  echo "application executable is not arm64-only" >&2
  exit 1
fi

ln -s /Applications "$stage_root/Applications"

signing_identity="${APPLE_SIGNING_IDENTITY:-}"
apple_id="${APPLE_ID:-}"
apple_team_id="${APPLE_TEAM_ID:-}"
apple_app_password="${APPLE_APP_PASSWORD:-}"

notary_value_count=0
for value in "$apple_id" "$apple_team_id" "$apple_app_password"; do
  if [[ -n "$value" ]]; then
    notary_value_count=$((notary_value_count + 1))
  fi
done
if [[ $notary_value_count -ne 0 && $notary_value_count -ne 3 ]]; then
  echo "APPLE_ID, APPLE_TEAM_ID, and APPLE_APP_PASSWORD must be provided together" >&2
  exit 2
fi
if [[ $notary_value_count -eq 3 && -z "$signing_identity" ]]; then
  echo "notarization requires APPLE_SIGNING_IDENTITY" >&2
  exit 2
fi

xattr -cr "$app"
if [[ -n "$signing_identity" ]]; then
  codesign --force --options runtime --timestamp --sign "$signing_identity" "$app"
else
  codesign --force --sign - "$app"
fi
codesign --verify --deep --strict --verbose=2 "$app"

dmg_output="$output_dir/RogueAssistant-$build_label-macos-arm64.dmg"
notary_zip="$output_dir/.RogueAssistant-$build_label-notarization.zip"
cmake -E rm -f "$dmg_output" "$notary_zip"

if [[ $notary_value_count -eq 3 ]]; then
  ditto -c -k --sequesterRsrc --keepParent "$app" "$notary_zip"
  xcrun notarytool submit "$notary_zip" \
    --apple-id "$apple_id" \
    --team-id "$apple_team_id" \
    --password "$apple_app_password" \
    --wait
  xcrun stapler staple "$app"
  cmake -E rm -f "$notary_zip"
fi

hdiutil create \
  -quiet \
  -format UDZO \
  -fs HFS+ \
  -volname "Emerald Rogue Assistant $build_label" \
  -srcfolder "$stage_root" \
  "$dmg_output"

if [[ -n "$signing_identity" ]]; then
  codesign --force --timestamp --sign "$signing_identity" "$dmg_output"
fi
if [[ $notary_value_count -eq 3 ]]; then
  xcrun notarytool submit "$dmg_output" \
    --apple-id "$apple_id" \
    --team-id "$apple_team_id" \
    --password "$apple_app_password" \
    --wait
  xcrun stapler staple "$dmg_output"
fi

test -s "$dmg_output"

hdiutil verify -quiet "$dmg_output"
dmg_verify_root="$(mktemp -d "$build_dir/macos-dmg-verify.XXXXXX")"
dmg_mounted=false
cleanup_mount() {
  if [[ "$dmg_mounted" == true ]]; then
    hdiutil detach -quiet "$dmg_verify_root" || return
  fi
  rmdir "$dmg_verify_root"
}
trap cleanup_mount EXIT
hdiutil attach -quiet -readonly -nobrowse -mountpoint "$dmg_verify_root" "$dmg_output"
dmg_mounted=true
for entry in "$dmg_verify_root"/*; do
  case "$(basename "$entry")" in
    RogueAssistant.app|Applications) ;;
    *) echo "unexpected installer item: $entry" >&2; exit 1 ;;
  esac
done
test "$(readlink "$dmg_verify_root/Applications")" = /Applications
cmake \
  "-DROGUE_INSTALL_ROOT=$dmg_verify_root" \
  -DROGUE_INSTALL_PLATFORM=macos \
  "-DROGUE_EXPECTED_VERSION=$version" \
  -P "$repo_root/cmake/VerifyInstall.cmake"
codesign --verify --deep --strict --verbose=2 "$dmg_verify_root/RogueAssistant.app"
if [[ "$(lipo -archs "$dmg_verify_root/RogueAssistant.app/Contents/MacOS/RogueAssistant")" != arm64 ]]; then
  echo "installer application executable is not arm64-only" >&2
  exit 1
fi
