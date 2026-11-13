# SIP messaging

Messages is the second tab, after Phone. Choose a sending account and a number
or SIP URI, then Open chat or write a message and Send. Enter sends; Shift+Enter
adds a line. Drafts remain separate while switching conversations/accounts.

The application layout remains left-to-right. Message text uses Unicode/UTF-8
and Qt's paragraph direction and shaping, including Persian joining, ZWNJ,
mixed Persian/English, emoji and multiline messages. Messages are plain text;
HTML-looking text is displayed literally and is never executed or fetched.

History is saved atomically in `messages.json` inside the active configuration
directory, with the account ID, peer URI, original text, time, send result and
local unread flag. Message bodies are not written to the application event log.
Unread badges do not switch tabs or steal focus. Removed-account conversations
remain readable and cannot silently send through a different account.

Sending uses liblinphone's Basic chat backend (SIP MESSAGE), over the account's
UDP/TCP/TLS transport, including isolated mutual-TLS accounts. A PBX/provider
must route MESSAGE, or the remote endpoint must accept it directly. Cellular
SMS gateways, attachments, group chat and end-to-end message encryption are
not part of this feature. The account's SRTP setting applies to call media.

Sent means a successful SIP acknowledgement, not that a person read the message.
Failures show the available SDK error, with an explicit Send again action. The
5.3 SDK sometimes discards SIP error details before notifying the application;
Stel shows a generic failure rather than inventing a SIP code. An unfinished
send on exit/restart is marked interrupted with an unknown delivery outcome;
messages are never automatically resent.

Verification covers exact UTF-8 payloads and replies through a loopback registrar,
two accounts in one core, independent cores and mutually authenticated TLS cores.
UI tests exercise input-method commits, Enter/Shift+Enter, literal text, unread
state, account selection, failed-send retry and history across application restarts.
