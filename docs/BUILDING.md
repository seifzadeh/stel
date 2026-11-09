# Building Stel

The version is defined once in `project(Stel VERSION X.Y.Z ...)` in
`CMakeLists.txt`. It drives `--version`, Settings → About, Windows Explorer
metadata, source/archive names and release tags.

## Linux x86_64 (Debian 13)

Use a clean Debian 13 machine/container with at least 4 GB RAM and sufficient
space for dependency sources. The CI setup script installs development packages
and enables `deb-src`; it is intended for build hosts.

```sh
sudo bash scripts/ci/install-linux-deps.sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DSTEL_QT_MAJOR=5 -DSTEL_DISTRO_LINPHONE=ON \
  -DSTEL_AV1_ADDON=ON -DSTEL_LEGACY_VIDEO_ADDON=ON
cmake --build build --parallel 4
bash scripts/ci/test-linux.sh build
python3 scripts/package-linux.py
python3 scripts/ci/smoke-portable.py dist/Stel-linux-x86_64.run
```

The distribution includes an audited ELF dependency closure, public CA bundle,
SIP grammars, codec plugins, license notices and a manifest. glibc and other
listed OS facilities remain external. Debian 13 packages determine the Linux
dependency versions; their exact binary/source versions are recorded in
`share/licenses/debian-sources.json`. CI downloads the matching `.dsc`, original
tarballs and Debian patches into the release's dependency-source archive.

The test wrapper starts a private D-Bus session and an unlocked GNOME Secret
Service in temporary XDG directories. A real `secret-tool` store/read/delete
preflight runs before CTest, and the temporary keyring is removed afterward.
This keeps credential-persistence tests enabled on a headless host without using
the developer's personal keyring or enabling plaintext password fallback.

The older `bootstrap-debian.sh` / `build-linux.sh` scripts also support the local
rootless development setup. Its `.deps` directory is not part of the repository.

## Windows x64

The automated recipe is in `.github/workflows/build-release.yml` and
`scripts/ci/windows-*.ps1`. It uses Windows Server 2022 to build:

- Qt 5.15.2 `win64_msvc2019_64` (official open-source Qt archives).
- MSVC 2019/v142 (14.29), Python 3.11 and CMake 3.31.6.
- Linphone SDK 5.3.105 and recursively pinned submodules, compiled with video,
  AV1, VP8, embedded OpenH264, G.729, SRTP and ZRTP.
- QtKeychain 0.15.0, compiled with the same Qt and compiler ABI.

Exact SDK/keychain commits are in `packaging/dependencies.json`. The Actions
recipe installs MSYS2 UCRT64 build tools, selects the x64 MSVC environment, then runs:

```powershell
./scripts/ci/windows-deps.ps1
./scripts/ci/windows-build.ps1
python scripts/ci/windows-sources.py
python scripts/ci/stage-assets.py windows
```

`QT_ROOT_DIR` must point to Qt. `windows-deps.ps1` defaults to `C:/stel-deps`;
`windows-build.ps1` uses that same location. Configure native MSVC before invoking
these scripts. The workflow caches installed dependencies and pinned sources.

MSYS2 tools use the maintained UCRT64 repository; application and SDK compilation
still use MSVC v142. Qt installation reuses the explicitly selected Python 3.11
instead of replacing it with another interpreter. A preflight checks that Python
and its SDK/packaging modules remain available after dependency actions.

The packager runs `windeployqt`, includes SDK/keychain DLLs, plugin/resource files,
MSVC CRT and Universal CRT redistributables, audits normal/delay PE imports with
`pefile`, and creates both a ZIP and a native single-file EXE. A clean-environment
smoke test executes the EXE alone from a path containing spaces and Persian text.
Only native binaries are needed on an end user's machine; Python/MSYS2 are build
tools. Dependency sources include the exact SDK/submodules/keychain and official
Qt module sources, with revision/checksum records.

The compiler, Qt version and CRT are selected with Windows 7/8.1 in mind. This
does not prove legacy compatibility. Test the exact output on Windows 7 SP1 and
8.1 VMs with their required system updates; Actions' Windows 2022 test host cannot
substitute for those checks. Windows builds have not been run on this Linux
development host.

## Official SDK / static builds

The default CMake path consumes official `LibLinphone` CMake targets. The Debian
path works around incomplete distro CMake exports. Leave the app's MS2 addon
options off with an SDK that already supplies those filters. The legacy FFmpeg 7
addon is for the tested Debian integration, not the Windows SDK.

`STEL_STATIC=ON` requires static Qt, QtKeychain and all SDK/dependency targets;
the configure step rejects shared targets. CI currently produces bundled runtime
packages rather than claiming fully static output. See `scripts/build-static.sh`
and `scripts/build-sdk.sh` for the separate static development path.

## Testing

`ctest` exercises native SIP/RTP/SRTP calls, account persistence, call controls,
software video, per-account auto answer/caller names, settings dialogs, and
mutual TLS/digest with generated certificates. Tests use local loopback peers.
For visual evidence, set `STEL_QA_SCREENSHOTS` when running `stel_ui_tests`.
See `docs/TEST_REPORT.md` for locally verified results and their limits.
