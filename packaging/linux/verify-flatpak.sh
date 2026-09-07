#!/usr/bin/env bash

set -euo pipefail

if [[ $# -ne 2 || "${GITHUB_ACTIONS:-}" != true ]]; then
  echo "usage: verify-flatpak.sh PACKAGE VERSION (on a disposable GitHub runner)" >&2
  exit 2
fi
package="$1"
version="$2"
app_id=assistant.emerald.rogue
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
if flatpak info --user "$app_id" >/dev/null 2>&1; then
  echo "refusing to replace an existing app installation" >&2
  exit 1
fi
python3 "$script_dir/verify-bundle.py" "$package" "$version"
flatpak install --user --noninteractive "$package"
test "$(flatpak run "$app_id" --version)" = "Emerald Rogue Assistant $version"
desktop="$HOME/.local/share/flatpak/exports/share/applications/$app_id.desktop"
test -f "$desktop"
grep -q 'Name=Emerald Rogue Assistant' "$desktop"
flatpak run --command=sh "$app_id" -c 'command -v xdg-open'
python3 "$script_dir/verify-startup.py"

data="$HOME/.var/app/$app_id/data/emerald-rogue-assistant"
test -s "$data/scripts/RogueAssistant_mGBA.lua"
script_hash="$(sha256sum "$data/scripts/RogueAssistant_mGBA.lua")"
installed_commit="$(flatpak info --user --show-commit "$app_id")"

# A second bundle with the same app files checks replacement without another build.
update_test="$(mktemp -d "$RUNNER_TEMP/rogue-flatpak-update.XXXXXX")"
flatpak build-export --subject="Package update check" "$update_test/repository" \
  "$script_dir/../../build/linux-flatpak/app" stable
flatpak build-bundle "$update_test/repository" "$update_test/update.flatpak" "$app_id" stable
flatpak install --user --noninteractive "$update_test/update.flatpak"
test "$(flatpak info --user --show-commit "$app_id")" != "$installed_commit"
test "$script_hash" = "$(sha256sum "$data/scripts/RogueAssistant_mGBA.lua")"
flatpak install --user --noninteractive "$package"
test "$(flatpak info --user --show-commit "$app_id")" = "$installed_commit"
test "$(flatpak run "$app_id" --version)" = "Emerald Rogue Assistant $version"
flatpak uninstall --user --noninteractive "$app_id"
test ! -e "$desktop"
test "$script_hash" = "$(sha256sum "$data/scripts/RogueAssistant_mGBA.lua")"
flatpak install --user --noninteractive "$package"
test "$(flatpak run "$app_id" --version)" = "Emerald Rogue Assistant $version"
test -f "$desktop"
test "$script_hash" = "$(sha256sum "$data/scripts/RogueAssistant_mGBA.lua")"
flatpak uninstall --user --noninteractive "$app_id"
echo "Flatpak install, menu entry, startup, bridge, update, and removal checks passed."
