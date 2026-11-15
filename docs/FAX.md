# Native fax

The main window's **Fax** tab has a send form above the received-fax table.
Select an account, destination and document, then choose the protocol for that
call. **T.38 is the default**; **Pass-through · G.711** is the other option.
The choice does not change the account's voice codec or media-security settings.

On a ringing or connected incoming call, press **Fax**, choose the protocol and
press **Receive fax**. The received document appears in the table, with sender,
account, protocol, confirmed page count and status. **Open** uses the internal
multipage viewer; **Save as** exports a TIFF copy. The call window retains Hang up
and reports fax progress. Its voice controls are disabled during a fax.

## Documents and completion

- Select **File · PDF / TXT / image** to import PDF, TXT, TIFF, PNG or JPEG.
  PDF and TIFF retain all pages (up to 100). PDF rendering is embedded; no
  external conversion program or network service is used. Password-protected
  PDFs need to be saved as an unlocked copy before import.
- Select **Type text**, then **Write / edit text…** to compose a fax. Persian,
  mixed Persian/English and ZWNJ are supported using the embedded Vazirmatn font;
  the application interface remains LTR. Long text wraps and paginates
  automatically; a form-feed character starts a new page. TXT accepts UTF-8,
  UTF-16 with BOM and legacy Windows-1256 Persian. Limits: 250,000 characters,
  1 MiB per TXT and 100 MiB per PDF.
- Every outgoing page is normalized to physical A4 (210 × 297 mm), with 10 mm
  safe margins. PDF/image proportions are preserved, with centering and white
  padding as needed; oversized pages fit within A4 instead of being cropped.
  Output is bitonal CCITT Group 4 TIFF at fine fax resolution, with standard
  1728-pixel T.4 scanlines and 2292 rows, 204 × 196 dpi. The wider T.4 scanline
  includes padding around physical A4; pixel aspect is accounted for in layout
  and preview. Already-normalized TIFF passes through without repeated scaling.
- **Preview A4** shows the actual prepared pages before sending, with Previous,
  Next and zoom controls. Conversion runs in the background so the SIP engine
  stays responsive. Cancel stops conversion before placing a call; during an
  active fax it hangs up that fax call.
- A fax is marked **completed** only after SpanDSP's T.30 completion callback
  confirms success and at least one page. A connected SIP call alone is not a
  successful fax. Failures and interrupted transfers remain in history; a
  partially received TIFF may be available for inspection.
- Files and history are under `faxes/` in the selected Stel configuration
  directory. Documents are local files; the app does not upload them to a web
  service. A damaged history file is preserved instead of being overwritten.

## Protocol behaviour

T.38 uses SpanDSP's version-0 terminal and UDPTL redundancy, up to 14,400 bit/s,
with ECM enabled. Calls normally start with G.711 and CNG/CED tones, then change
to T.38 using a SIP re-INVITE. Initial `m=image ... udptl t38` incoming offers are
also understood. The PBX, trunk and other endpoint must support the negotiation.
If T.38 fails, Stel reports the failure; it does not silently send that document
using another protocol. Start a new fax with G.711 when appropriate.

UDPTL is unencrypted and is not SRTP. T.38 therefore requires an account whose
media setting is RTP. SIP signalling can still use TLS/mutual TLS. Use G.711 if
the account requires SRTP. G.711 fax uses the account's ordinary RTP/SRTP path;
it offers only PCMU/PCMA for that call and connects a private 8-kHz fax modem in
place of microphone and speaker. Hardware audio devices are not needed.

The UDPTL socket binds to the selected media IP (IPv4 by default), on an available
UDP port. It is independent of the audio/video RTP port pools. T.38 does not use
the account's ICE/TURN relay: use a reachable media IP or a PBX that relays UDPTL,
and permit that traffic through the firewall. FEC and TCP-carried T.38 are not
implemented in this profile.

## Build and corresponding source

Stock liblinphone 5.3.105 has no public native fax endpoint API. The maintained
`packaging/patches/linphone-native-fax.patch` adds opt-in external T.38 SDP handling
and virtual audio-device refresh. The application owns SpanDSP and its socket;
Linphone still owns SIP authentication, TLS, dialogs and re-INVITEs. Ordinary
audio/video calls keep the existing negotiation path. Belle-SDP's integer-only
media format representation is normalized at the SDP boundary for T.38's
non-numeric `t38` token.

On Linux, install the normal development dependencies plus `libspandsp-dev` and
`libtiff-dev`, then build the pinned SDK:

```sh
python3 scripts/build-fax-sdk.py --output "$PWD/.deps/fax-sdk"
export STEL_FAX_SDK="$PWD/.deps/fax-sdk"
export STEL_PDF_SDK="$PWD/.deps/pdf-sdk"
python3 scripts/build-pdf-engine.py --output "$STEL_PDF_SDK"
export LD_LIBRARY_PATH="$STEL_FAX_SDK/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
cmake -S . -B build -G Ninja -DSTEL_DISTRO_LINPHONE=ON \
  -DSTEL_LINPHONE_ROOT="$STEL_FAX_SDK" -DSTEL_PDF_ROOT="$STEL_PDF_SDK"
cmake --build build --parallel 4
```

`scripts/build-linux.sh` also performs these builds. Packaging requires
`STEL_FAX_SDK` and `STEL_PDF_SDK`; it embeds the exact SDK source archive, patch and build options
alongside its notices. Source URLs and SHA-256 digests are pinned in
`packaging/dependencies.json`.
MuPDF uses its upstream static-library build. Its dependency notices accompany
the binary and the exact original release archive accompanies release dependency
sources. The bundled Vazirmatn font retains its SIL OFL license.

The Windows dependency workflow applies the same patch to the pinned SDK and
builds libtiff and the SpanDSP fax source subset with MSVC, using the wrapper in
`cmake/FaxSpandsp/`. The DLL and its notices are included in the portable package;
matching dependency sources accompany releases. The wrapper generates upstream
modem filters and supplies modern MSVC and UTF-8 filename compatibility.
The same workflow builds MuPDF's upstream MSVC solution with v142 and links its
static libraries into the app; no extra PDF runtime DLLs are required.

`stel_fax_tests` verifies T.30 image fidelity, UDPTL parsing and packet loss,
SIP/re-INVITE transfers, two accounts in one core, received history and the Fax
widget. Tests use generated documents and a loopback PBX, not real extensions.
PDF/TXT/typed-text input is also sent through the loopback fax tests. The separate
`stel_fax_document_tests` checks mixed PDF page sizes, A4 physical proportions,
safe margins, Persian text pagination/encodings, cancellation and the preview UI.

References: [SpanDSP T.38 terminal API](https://www.soft-switch.org/spandsp-doc/t38__terminal_8h.html),
[Linphone SDP implementation](https://github.com/BelledonneCommunications/liblinphone/tree/5.3.105/src/sal).
