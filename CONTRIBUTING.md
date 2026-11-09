# Contributing

Stel is maintained by Mehrdad Seifzadeh. Issues and pull requests are welcome at
<https://github.com/seifzadeh/stel>.

Read [BUILDING](docs/BUILDING.md) before changing the application. Keep account
credentials and client private keys outside the repository. Use generated local
TLS fixtures and loopback SIP accounts for tests; never commit a working PBX
configuration or a real password. Dummy audio/video devices make call tests
possible without physical hardware.

Prefer small changes that preserve Qt 5 compatibility and the separation between
settings/models, SIP engine and UI. Run the relevant native tests and the full
CTest suite for call-lifecycle or media changes. Avoid calling call-control SDK
methods recursively inside SDK state callbacks; queue decisions on the Qt loop.

Contributions to original Stel code use GPL-3.0-or-later. Keep upstream notices
and licenses in third-party files. Document any changes to vendored filters and
keep their matching source origins/checksums. See NOTICE.md and THIRD_PARTY.md.

The maintainer controls releases by updating the CMake project version and
CHANGELOG.md. See [RELEASE](docs/RELEASE.md) for automated publication behavior.
