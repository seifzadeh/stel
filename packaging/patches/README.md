# Windows SDK compatibility patches

`mbedtls-msvc-psa-initializers.patch` applies to the exact Mbed TLS gitlink in
Linphone SDK 5.3.105. It hides eight redundant forward declarations when compiling
as C++ with MSVC; complete static-inline definitions still follow their structure
definitions. MSVC v142 otherwise rejects the incomplete C return types with C2526.
Other compilers and C compilation retain the original declarations. No crypto
implementation, security checks, layout or exported ABI is changed.

Upstream diagnosis: <https://github.com/Mbed-TLS/mbedtls/issues/7654>.
Application: `python scripts/ci/patch-sdk.py <sdk-source>` before configuring it.
The script checks both upstream revisions and supports repeated execution.
The Windows dependency-source archive includes the patched headers; this patch
and recipe are in Stel's source archive and the binary's third-party notices.
Original Mbed TLS license/copyright notices are retained.

`libyuv-install-import-library.patch` adds the missing `ARCHIVE` install destination
to the pinned SDK's libyuv target. On Windows this installs `yuv.lib` beside the
other SDK import libraries and records it in `YUVTargets-release.cmake`, allowing
Stel to link the existing DLL's RGB converter. It also permits static libyuv
installation. The codec implementation and ABI are unchanged. The patched CMake
source and this recipe are included in the dependency/application source archives.
