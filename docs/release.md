# Release process

This page explains how to build, test, and publish Emerald Rogue Assistant packages.

## Version

`cmake/Version.cmake` sets the three-part version number. The full version is
chosen when CMake runs:

- A development build includes the commit ID, such as
  `1.0.0-dev.g5c742bbe54fe`. Its package label is `dev-5c742bbe54fe`.
- A named release uses its Git tag. For example, `v1.0.0-alpha.0` produces
  version `1.0.0-alpha.0`.

Local changes add a `dirty` label. Source files without Git history use an
`unknown` label. Build packages for testers from a Git checkout with no
uncommitted changes. Named releases require this too, and the checked-out
commit must match the specified tag.

CMake uses the full version in the UI, logs, and command output. Package names
use the release version or development label.
Windows and macOS system fields use the three-part number when they require a
number. The bridge protocol, multiplayer protocol, Home Box format, and ROM
Assistant API have their own versions.

Package names are generated automatically. In this list, `<build>` is the
release version or development label. Do not rename packages to change their
version; that would leave the version inside the app unchanged.

- `RogueAssistant-<build>-windows-x64.zip`
- `RogueAssistant-<build>-macos-arm64.dmg`
- `RogueAssistant-<build>-linux-x86_64.AppImage`
- `RogueAssistant-<build>-linux-x86_64.tar.gz`
- `THIRD_PARTY_NOTICES.md`
- `SHA256SUMS`

The macOS DMG contains the app and an Applications shortcut. Third-party
notices, dependency licenses, and guides are inside the app, under
`Contents/Resources/Documentation`. There is no separate macOS app ZIP.
Windows and Linux packages also include the notices, licenses, and guides.

## Local package builds

Use only the preset for the current system. Each preset is a saved set of
build options. These commands also require Ninja, the tool that runs the build.

On Windows:

```sh
cmake --preset release-windows-x64 --fresh -G Ninja
cmake --build --preset release-windows-x64 --parallel
ctest --preset release-windows-x64
cpack --preset release-windows-x64
```

On macOS:

```sh
cmake --preset release-macos-arm64 --fresh -G Ninja
cmake --build --preset release-macos-arm64 --parallel
ctest --preset release-macos-arm64
bash packaging/macos/package.sh build/release-macos-arm64 dist
```

On Linux:

```sh
cmake --preset release-linux-x86_64 --fresh -G Ninja
cmake --build --preset release-linux-x86_64 --parallel
ctest --preset release-linux-x86_64
cpack --preset release-linux-x86_64
```

The Linux AppImage also needs the x86_64 linuxdeploy build named
`1-alpha-20251107-1`. Its SHA-256 value is
`c20cd71e3a4e3b80c3483cef793cda3f4e990aca14014d23c544ca3ce1270b4d`.
The GitHub workflow downloads and checks this exact file.

All release presets treat project warnings as errors. The macOS preset targets
macOS 11 and builds only arm64. Check the result with:

```sh
lipo -archs build/release-macos-arm64/RogueAssistant.app/Contents/MacOS/RogueAssistant
```

The output must be `arm64`.

## GitHub Actions

The `Build packages` workflow supports two ways to run:

- Start it by hand from a branch to build development packages. Their names
  include the commit ID. The files appear under **Artifacts** on the workflow
  run page. This does not create a GitHub release.
- Push a version tag, such as `v1.0.0-alpha.0`, to build a named release. This
  also creates or updates a draft GitHub release.

The workflow passes the tag to CMake through `ROGUE_RELEASE_TAG`. CMake checks
that the tag points to the current commit and its three-part number matches
the source. Without that option, even a tagged commit builds as a development
version. To build a named release locally, check out the tag and add
`-DROGUE_RELEASE_TAG=v1.0.0-alpha.0` to the configure command.

The workflow builds and tests all three systems, checks the installed files,
creates packages, and writes `SHA256SUMS`. An alpha, beta, or release-candidate
version becomes a GitHub prerelease. The workflow always leaves the GitHub
release as a draft. A person must check and publish it. A rerun can replace
files in a draft, but it refuses to replace files in a published release.

## macOS signing

The macOS job always signs the app. Without Apple credentials, it uses an
ad hoc signature. This is a local signature, not proof of the developer's
identity. Users may need to approve the app in macOS settings before opening it.

To sign with a Developer ID and send the app to Apple for its automated security
check (notarization), add these repository secrets:

- `MACOS_CERTIFICATE_P12`
- `MACOS_CERTIFICATE_PASSWORD`
- `MACOS_SIGNING_IDENTITY`
- `APPLE_ID`
- `APPLE_TEAM_ID`
- `APPLE_APP_PASSWORD`

The three `MACOS_` values must be present together. The three `APPLE_` values
must also be present together and require all three `MACOS_` values. The job
fails if only part of a set is present.

The job uses a temporary keychain and deletes it at the end. It checks the app
signature and DMG before it uploads them.

## Publish a release

Before you create the release tag:

1. Choose the release version. Update the three-part number in
   `cmake/Version.cmake` only if that number changes. The tag supplies labels
   such as `alpha.0`, `beta.1`, or `rc.1`.
2. Confirm that the change is on the intended default-branch commit.
3. Confirm that normal hosted CI passes on that commit.
4. Complete the live mGBA checks that are possible on the current system.
5. Create and push a signed tag for the chosen version, such as
   `v1.0.0-alpha.0`.

After the release workflow finishes:

1. Confirm that every job passed.
2. Download `SHA256SUMS` and all package files from the draft release.
3. Check every file against `SHA256SUMS`.
4. Install and start the package for the system you can test.
5. Check its version, CPU type, resources, and package contents. On macOS, also
   check the signature and installer image.
6. Read the draft notes and state all live test gaps.
7. Publish alpha, beta, and release-candidate drafts as prereleases. Publish a
   final version without the prerelease marker.

State which systems and ROM editions still need testing. Alpha and beta
releases let other users help test those combinations.

## Tests required for final 1.0.0

The normal test suite must pass with MSVC x64, Apple Clang, GCC, and Clang.
The memory and C++ runtime checks and the Lua job must also pass. The Clang
build includes source checks with `clang-tidy`.

Automated tests cover:

- ROM Assistant API 3 checks for Vanilla and EX
- ROM headers, changing state, confirmation writes, Home Box, and multiplayer
- Request IDs, bad results, queue limits, and paused mGBA
- Bridge messages split across reads or joined in one read, partial sends,
  rejected connections, and reconnects
- Original Home Box file compatibility, file damage, backups, and interrupted
  writes
- Multiplayer compatibility and ROM layout checks
- Worker shutdown and UI thread boundaries
- Lua and C++ use of the same byte examples

The following live checks are also required:

- Start each package on a clean Windows x64, macOS arm64, and Linux x86_64
  system.
- Check resources, script export, settings, and user file paths on each system.
- Test Home Box load, save, old file import, and backup recovery with real game
  data.
- Test both host and client between Windows and macOS.
- Test one multiplayer pair that includes Linux.
- Check clear errors for a wrong protocol, ROM API, edition, player count, and
  ROM layout.
- On each system, test both start orders, pause and resume, ROM reset, script
  reload, app restart, mGBA shutdown, and app shutdown.

Record the app version, mGBA version, ROM version, ROM edition, ROM API,
operating system, CPU type, and result for each release candidate. Do not tag
the final release if a required check fails, mGBA crashes, data can be lost
without a warning, or a queue can grow without a limit.

## Work after 1.0

These changes are outside the 1.0 release:

- Automatic local pairing for more than one mGBA session
- Tested support for ROM Assistant APIs 1 and 2
- Linux arm64 packages
- Windows x86 packages if users need them
- Optional update checks
