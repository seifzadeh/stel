# Windows SDK compatibility patch

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
