# Implementation and extension boundary

`model` owns versioned JSON settings, contacts and capped call history.
Credentials are omitted from JSON and stored through QtKeychain by default,
with explicit session-only opt-out. Each configuration directory has its own
credential namespace. Read failures cannot overwrite existing secrets with an
incomplete snapshot. `SipEngine` owns a liblinphone
core, accounts/auth references, stable UUID call handles and callbacks. A Qt timer
iterates the core every 20 ms on the GUI thread; SDK objects are never accessed
from a worker thread. Incoming forwarding and transfer completion are queued out
of SDK callbacks to avoid nested call state changes.

New accounts and settings missing a media-mode field default to RTP (`none` in
JSON). The SIP form displays RTP/SRTP/DTLS-SRTP/ZRTP using stable item data, so the
visible label is separate from the saved enum. Explicit saved choices survive
loads. SDK offer/answer retry is disabled with
`sip/retry_invite_after_offeranswer_failure=0`: SDK 5.3 otherwise checks a core-wide
flag and may retry mandatory per-call SRTP as plaintext. A core-wide mandatory
flag would also disrupt mixed incoming account modes, so per-call policy remains
in use. Answering a required-SDES account checks the remote offer first and
returns SIP 488 for missing SRTP instead of emitting a 200 response with all audio
streams disabled. DTLS policy is enforced at the SDK handshake stage.

Account `autoAnswer` defaults false. Incoming policy decisions are queued outside
the SDK callback, rechecking the UUID, current state and current account config
before acting. Configured forwarding takes priority, then DND rejection, then
per-account automatic audio Answer through the normal answer/shared-media path.
Stale/rejected calls are not answered. Explicit camera consent remains separate.

Account `callerIdName` is distinct from the local `label`. The SDK address builder
sets the identity display name, producing a standard From name-addr without raw
header concatenation. UTF-8, spaces, punctuation, quotes and backslashes are
covered by wire tests that parse the real INVITE. Blank explicitly stored names
remain blank; legacy settings missing the field inherit the old label to preserve
the previously transmitted name. Control characters and excessive names are
rejected. SIP usernames and authentication IDs remain separate fields.

liblinphone 5.3.105's TLS fallback picks the first TLS auth-info in a core. Each
mTLS identity therefore receives a child core with its own account, transports,
data directory and credentials. The facade forwards operations and events by
stable account/call IDs. Two different client certificates against the same
registrar are tested. Attended transfer and local mixing across these isolated
cores require server-side bridging; the app rejects those operations with a
clear message. Multiple calls within one account support both operations.

Incoming calls prefer the To identity when mapping to a configured account,
because PBXs can deliver several registrations to a shared, userless Contact.
When an incoming call is the peer of an outgoing call to another local account,
answering temporarily enables the SDK's shared media resource mode. Exclusive
mode would terminate the still-ringing outgoing leg while accepting its peer.
The two local call references are tracked until release, then exclusive mode is
restored. Ordinary incoming calls retain the normal exclusive/preemption policy.
The local PBX regression uses distinct Call-IDs for both legs, a userless
Request-URI, real SIP/SDP, SRTP/ICE, and received PCM recordings on both legs.

`MainWindow`, `CallDialog`, `SettingsWidget` in a separate QDialog, and `AccountEditor` implement
Qt Widgets UI; they do not construct SIP packets. Closing Settings preserves the
main window and registrations. Confirmed application exit explicitly shuts down
child certificate cores before the parent core, releasing calls and listeners.
A QLockFile/QLocalServer pair activates the existing instance for a repeated
launch using the same configuration directory.

`MainWindow` indexes modeless call dialogs by stable call UUID, independently of
which active-list row is selected. Closing a live dialog through the title-bar X,
Escape/reject or the footer Close window button confirms disconnecting that UUID.
No (also the default/Escape response) preserves both the dialog and session; Yes
hangs up/rejects the call and closes the dialog only if the request succeeds.
A guard prevents nested confirmations. End/release during the nested event loop
dismisses the question without another termination request. Already-ended windows
close without confirmation. Explicit Hang up/Reject still ends the call directly. End/error windows disable controls and expire after
a short grace period. SDK shutdown precedes destruction of native video surfaces.
Each call supplies its own remote surface ID; the SDK's preview target is
core-scoped and is rebound when a video dialog is shown. Automatic standalone SDK
video windows are disabled. Incoming video offers require explicit video consent.

`VideoView` registers a per-factory MS2 display sink through a pre-start engine
installer that is inherited by isolated certificate cores. The sink converts
I420 to RGB with the SDK scaler on the media thread, retaining only the newest
frame per live surface under a mutex. A 40 ms GUI timer consumes the frame and
Qt paints it with its aspect ratio preserved. No QWidget is touched on a media
thread. Surface deregistration is synchronized with frame publication. This
avoids XVideo-port requirements, SDK GLX initialization crashes and OpenGL/QML
rendering dependencies for the call display; Linux and Windows use the same
Qt raster path.

DTMF, mute, recording, hold and transfers target the dialog's UUID. Ringing
Transfer uses 302 redirect; established Transfer uses REFER. Consultation waits
for the original call to reach Paused before opening the dialer. Attended transfer
and conference explicitly select the peer UUID. Local mixing adds selected calls
with the single-participant API, which normalizes LinphoneStatus in SDK 5.3;
the bulk API exposes the underlying boolean return. Conference account identity
is explicit, including when all calls are paused. Conference microphone/recording
controls operate on the shared conference audio, and readiness is checked before
changing the microphone. Independent calls retain individual microphone state.

The release folder starts through its native executable with relative ELF paths
and internally discovered Qt/SDK resources. The native single-file launcher
extracts a bounded embedded payload into a private temporary directory, directly
starts that native executable, waits for its exit, and removes the directory.
It invokes no shell, archive command, FUSE mount or externally installed Qt.
The Linux launcher links statically with zlib; the Windows launcher uses a plain
payload and static CRT. SDK/OS compatibility limits still apply to the payload.

CMake uses the SDK's exported dependency graph for source/static SDK
builds. Direct shared-library discovery is an explicit Debian-only workaround.

The present `SipEngine` is a concrete adapter, not a finished interchangeable
backend framework. For future WebRTC/SCTP work, extract its public operations and
signals into an abstract `CallEngine`, move native handles behind a private
implementation, and add a capability object before adding any transport choice.
Keep `Account::validate` rejecting unsupported transports until a backend exists.

Two different future projects must not be confused:

1. Browser/WebRTC interoperability: prefer a SIP/WebRTC gateway on the PBX edge.
   Use the existing ICE/DTLS-SRTP/BUNDLE profile, codecs common to both endpoints,
   and explicitly test WSS-to-SIP signalling conversion on the gateway. For a
   native WebRTC backend, select libwebrtc/libdatachannel based on whether full
   media capture/processing or just peer/data channels are needed; implement the
   service's signalling and session/auth model as well.
2. SIP over SCTP: implement an SDK transport extension or select a maintained SIP
   engine with an actual SCTP transport and Windows implementation. A PJSIP enum
   named SCTP is not a working transport. SCTP data channels over DTLS do not
   provide SIP-over-SCTP.

Release work still required beyond local loopback verification: PBX test matrix,
real devices and NAT/TURN networks, hot-plug device handling, incoming video
upgrades/consent, credential-vault integration, crash recovery, code signing,
Windows dependency/OS-version validation, static plugin registration, reproducible
SDK builds and corresponding-source packaging. These are explicit release tasks,
not claims of this starter's completed behaviour.
