# Remote Control API

Stel 0.15 adds remote control to the running desktop application. Calls and API
commands use the same SIP engine; you can see an API call and operate it on the
main phone display. The application must be running.

## Enable it

1. Open **Settings → Remote API** and enable Remote Control.
2. Keep `127.0.0.1:8877` for local programs. For a different computer, select a
   local LAN IP and add the client's IP or subnet to **Allowed client IPs / CIDRs**.
3. Generate a token, copy it and apply. Only its SHA-256 hash is saved. A new
   token replaces the old one; it cannot be recovered after restarting Stel.
4. For HTTPS, select a matching PEM certificate chain and unencrypted PEM private
   key. Trust its CA in the client. HTTPS uses the existing Linphone SDK TLS
   backend, including on Windows; no separate OpenSSL installation is needed.

Use `Authorization: Bearer TOKEN`. For Yealink integrations that use HTTP Basic,
use username `stel` and the token as password. The same authentication and IP
allow-list protect every endpoint, including screenshots and downloads.

```sh
# Set these in your own terminal; do not put a real token in source control.
STEL_API='http://127.0.0.1:8877'
read -rsp 'API token: ' STEL_TOKEN; echo
curl -H "Authorization: Bearer $STEL_TOKEN" "$STEL_API/api/v1/status"
```

The API starts disabled. It accepts explicit IPs/CIDRs, not `any`. HTTP is useful
on loopback or a trusted LAN; use HTTPS when crossing untrusted networks. Browser
Origin requests/CORS, query-string tokens, HTTP chunked uploads and pipelining
are not supported. Send JSON as UTF-8 with `Content-Type: application/json`.
Request bodies are limited to 16 MiB, headers to 16 KiB, and connections to 16.

A small Python 3 client is included at [`scripts/stel-api.py`](../scripts/stel-api.py).
It uses only the standard library; Python is needed by this optional client, not
by the Stel application. Examples after setting the variables above:

```sh
export STEL_API STEL_TOKEN
python3 scripts/stel-api.py status
python3 scripts/stel-api.py dial 1002 --account ACCOUNT_UUID
python3 scripts/stel-api.py answer CALL_UUID
python3 scripts/stel-api.py fax-send 1002 --account ACCOUNT_UUID --file letter.pdf
python3 scripts/stel-api.py fax-receive CALL_UUID --protocol t38
```

## Yealink Action URI compatibility

Stel implements these HTTP Action URI names and paths from the
[Yealink Remote Control documentation](https://support.yealink.com/docs/sip-t73w/remote-control/1f2db889c3154b68b5aaf33436405e1e).
The three supported paths are `/servlet`, `/cgi-bin/ConfigManApp.com` and
`/cgi-bin/cgiServer.exx`. All return **Stel JSON**, with a successful command
returning HTTP 200. This is command/URI compatibility, not an emulation of
Yealink firmware, its provisioning response format, uaCSTA or SIP ACTION-URI NOTIFY.

| GET query | Action |
| --- | --- |
| `key=number=1002&outgoing_uri=1001@pbx.example` | Dial using that local SIP identity |
| `key=ANSWER`, `key=ASW`, `key=Asw` | Answer with audio; add `video=1` to accept video |
| `key=CALLEND`, `key=CallEnd`, `key=CANCEL`, `key=ONHOOK` | End/cancel the call; reject if ringing |
| `key=REJECT` | Reject a ringing incoming call (Stel extension) |
| `key=HOLD`, `key=F_HOLD` | Toggle Hold/Resume |
| `key=UNHOLD` | Resume |
| `key=MUTE` | Toggle microphone mute |
| `key=BTrans=1003` | Blind transfer |
| `key=F_TRANSFER&target=1003` | Transfer; Stel requires the destination explicitly |
| `key=ATrans=1003` | Hold, dial a consultation, then request attended transfer when answered |
| `key=ATrans=CALL_A@CALL_B` | Request attended transfer between two existing call IDs |
| `key=F_CONFERENCE&callId=CALL_A&otherCallId=CALL_B` | Join the two calls |
| `key=DNDOn`, `key=DNDOff` | Set Do Not Disturb |
| `key=5`, `key=*`, `key=POUND` | Send one DTMF digit during a call |
| `key=L1` … `key=L12` | Press the corresponding configured expansion key |
| `key=RD` | Redial the most recent call |
| `phonecfg=get` | Get the Stel status snapshot |

Use `callId=UUID` to select a call. `ASW:UUID`, `CANCEL:UUID`, `HOLD:UUID` and
`UNHOLD:UUID` also work. IDs come from `/api/v1/calls`; they are Stel UUIDs.
Without an ID, Stel uses the selected phone line, or the only eligible call.
Ambiguous calls/accounts need an explicit ID. Omit `outgoing_uri` to use the
selected calling account, or the only enabled account.

Percent-encode destinations containing `&`, `;`, `#`, `@` or `+` using your HTTP
client's query encoder. `number=`, `BTrans=` and `ATrans=` take the entire remaining
key value as their destination. Other supported keys can be separated by `;`.
They execute in order, stop at the first failure and are not a transaction.
Unsupported keys return 422; they never silently succeed. Automatic consultation
has a 45-second timeout and reports its progress in `transfer.changed` events.

```sh
curl -u "stel:$STEL_TOKEN" --get "$STEL_API/servlet" \
  --data-urlencode 'key=number=1002' \
  --data-urlencode 'outgoing_uri=1001@pbx.example'

curl -u "stel:$STEL_TOKEN" --get "$STEL_API/servlet" \
  --data-urlencode 'key=ASW:CALL_UUID'
```

`GET /screencapture` and `/screencapture/download` return the main display as JPEG.
`GET /api/v1/screen` is an alias. Settings windows are not part of this capture.

## JSON API

The base path is `/api/v1`. `GET /api/v1` lists resources. IDs and API field names
are case-sensitive. Successful mutations mean the local request was accepted;
SIP negotiation, remote transfer acceptance and fax delivery finish asynchronously.
Read the resource or its events to get the final result.

| Request | Body / result |
| --- | --- |
| `GET /status` | Version, accounts, calls, BLF, faxes, unread messages, transfer jobs and event cursor |
| `GET /calls` / `GET /calls/ID` | Active call IDs, peer/account, state, duration, codecs, media security and bandwidth |
| `POST /calls` | `{ "accountId":"…", "target":"1002", "video":false }`; 202 with `call.id` |
| `POST /calls/ID/answer` | `{ "video":false }` |
| `POST /calls/ID/reject` / `hangup` | `{}`; Reject only applies to ringing incoming calls |
| `DELETE /calls/ID` | Hang up |
| `POST /calls/ID/hold` / `resume` | `{}` |
| `POST /calls/ID/mute` / `unmute` | `{}` |
| `POST /calls/ID/record` / `stop-recording` | `{}`; use the normal local recording directory |
| `POST /calls/ID/dtmf` | `{ "digit":"5" }`; one digit per request |
| `POST /calls/ID/transfer` / `forward` | `{ "target":"1003" }`; Forward redirects a ringing call |
| `POST /calls/ID/attended-transfer` / `conference` | `{ "otherCallId":"…" }` |
| `POST /calls/ID/fax` | `{ "protocol":"t38" }`; receive fax on an incoming call |
| `GET /accounts` / `GET /accounts/ID` | Account configuration and registration state, without secrets |
| `POST /accounts` | Add account; `username`, `domain`, optional `id`, `password` and other account fields |
| `PATCH /accounts/ID` / `PUT /accounts/ID` | Update supplied fields; omitted passwords are retained |
| `DELETE /accounts/ID` | Remove account; active calls must end first |
| `POST /accounts/ID/register` / `unregister` | Save and apply the registration choice |
| `POST /dnd` | `{ "enabled":true }` |
| `GET /preferences` / `PATCH /preferences` | Read/update devices, SIP/media IPs and ports, codec lists, DND and expansion configuration |
| `GET /devices` | Available input, output and camera IDs, including dummy devices |
| `GET /codecs` | Available audio/video codec IDs and enabled state |
| `GET /blf` / `GET /expansion` | 12 key configurations, status, numeric state and colour |
| `PUT /expansion/N` | `{ "type":"blf", "label":"Support", "accountId":"…", "target":"1002" }` |
| `POST /expansion/N/press` | Immediate dial using that key's account and destination |
| `GET /messages` | History; optional `accountId` and `peer` query filters |
| `POST /messages` | `{ "accountId":"…", "target":"1002", "text":"سلام" }`; 202 with `messageId` |
| `POST /messages/ID/read` | Mark read in the API and chat UI |
| `GET /contacts` / `PUT /contacts` | Read/replace `{ "contacts":[{"name":"Support","uri":"1002"}] }` |
| `GET /history` | Recent call history |
| `GET /events?since=ID` | Changes after a cursor; see below |

Account fields match the settings model: `label`, `callerIdName`, `username`,
`domain`, `server`, `authUser`, `transport`, `encryption`, `register`, `enabled`,
`ice`, `webrtc`, `stunServer`, `turn`, `turnUser`, `expires`, `autoAnswer`,
`pagingEnabled`, `pagingMicMuted`, `pagingBeep`, `forwardTo`, `forwardBusy` and
`rememberPassword`. Password fields are write-only (`password`, `keyPassword`,
`turnPassword`). Certificate/private-key paths refer to files already on the
Stel machine; file contents and secret values are never returned.

Close Settings before editing configuration through the API; otherwise it returns
409 to preserve the user's edits. Media/network/codec changes also require no
active calls. Runtime call commands continue working while Settings is open.
Remote API credentials/listener configuration can only be changed locally in Settings.

BLF `stateCode`: 0 unknown, 1 subscribing, 2 available, 3 ringing, 4 busy, 5 error.
`colour` is green for available, red for busy/ringing and grey otherwise. Status
comes from actual SIP dialog subscriptions, not a guessed idle state.

## Fax from text or files

| Request | Body / result |
| --- | --- |
| `POST /documents` | `{ "text":"سلام\nFax test" }`, or `{ "filename":"letter.pdf", "base64":"…" }`; 202 with `document.id` |
| `GET /documents` / `GET /documents/ID` | Conversion state, page count and A4 dimensions |
| `GET /documents/ID/file` | Prepared TIFF, once state is `ready` |
| `DELETE /documents/ID` | Delete an unused completed API document |
| `POST /faxes` | `{ "accountId":"…", "target":"1002", "documentId":"…", "protocol":"t38" }`; 202 with fax ID |
| `GET /faxes` / `GET /faxes/ID` | Sent/received history, progress, page count and result |
| `POST /calls/ID/fax` | Receive fax on the ringing/connected incoming call |
| `DELETE /faxes/ID` | Cancel an active fax by ending its call |
| `GET /faxes/ID/file` | Completed sent/received TIFF |

Uploads accept PDF, UTF-8 TXT, TIFF, PNG and JPEG. Typed text supports Persian.
Conversion runs in the background and produces the same A4 format as the GUI:
1728 × 2292 pixels at 204 × 196 dpi. Poll until `ready` before sending. The limit
is two concurrent conversions and 128 prepared documents; delete unused ones.
Prepared documents remain available after restarting Stel. Downloads are limited
to 64 MiB and to known prepared documents/completed fax jobs; arbitrary local
file paths cannot be downloaded.

`protocol` defaults to `t38`; use `g711` for pass-through. T.38 uses UDPTL and needs
an RTP account; it cannot be used with mandatory SRTP. The PBX and peer must
support the chosen fax mode. Receive using the same mode as the sender. A
successful HTTP command is not a fax-delivery receipt: wait for `completed` and
check `pages`, or read `failed` and `detail`.

Example preparation and send:

```sh
curl -H "Authorization: Bearer $STEL_TOKEN" -H 'Content-Type: application/json' \
  -d '{"text":"سلام\nStel fax test"}' "$STEL_API/api/v1/documents"

# After GET /documents/DOCUMENT_UUID reports ready:
curl -H "Authorization: Bearer $STEL_TOKEN" -H 'Content-Type: application/json' \
  -d '{"accountId":"ACCOUNT_UUID","target":"1002","documentId":"DOCUMENT_UUID","protocol":"t38"}' \
  "$STEL_API/api/v1/faxes"
```

## Changes and errors

Poll `/events?since=LAST_ID` about once a second. It returns up to 500 retained
events, `lastEventId`, `oldestEventId`, `eventSession` and `resetRequired`. Keep
IDs as decimal strings. Start with `/status`, retain its `lastEventId` and
`eventSession`, then poll events. On a changed session or `resetRequired`, fetch a
fresh status snapshot. Cursors are in memory and reset when the application restarts.

Events include `call.changed`, `call.finished`, `account.changed`, `blf.changed`,
`message.changed`, `message.read`, `fax.changed`, `document.changed`,
`transfer.changed`, `dnd.changed`, `configuration.changed`, `engine.error` and
`engine.notification` (including asynchronous transfer notifications). Status polling
provides current durations/bandwidth. There is no outbound Action URL webhook
configuration in this release.

Errors use `{ "ok":false, "error":{"code":"…","message":"…"} }`.
401 means missing/wrong credentials; 404 means a missing resource; 409 means an
operation does not fit its current state; 422 means invalid input; 503 means a
configuration operation is busy. SIP and fax operations still follow the account's
existing TLS, media-encryption, forwarding and paging policies.

## راه‌اندازی و استفاده

در **Settings → Remote API** فعالش کن. برای برنامه‌ای روی همین سیستم، IP را
روی `127.0.0.1` و پورت را روی `8877` بگذار. برای سیستم دیگر، IP شبکهٔ خودت را
انتخاب کن و IP یا subnet آن کلاینت را در فهرست مجاز وارد کن. با **Generate token**
توکن بساز، کپی کن و **Apply** بزن. فقط هش توکن ذخیره می‌شود؛ بعد از اجرای مجدد
برنامه نمی‌توان توکن قبلی را دید. برای اتصال امن، گواهی و کلید PEM را هم انتخاب کن.

فرمان‌های تماس ییلینک، مثل `number=1002`، `ANSWER`، `CALLEND`، `HOLD`، `MUTE`،
`BTrans` و `ATrans` از مسیر `/servlet` قابل استفاده‌اند. نام فرمان و مسیر سازگارند؛
خروجی، JSON خود Stel است. این نسخه شبیه‌ساز فریمور ییلینک یا CSTA نیست.
جدول بالا فرمان‌های پشتیبانی‌شده را دقیق نشان می‌دهد.

برای کار کامل‌تر، از `/api/v1` استفاده کن: تماس، پاسخ و رد، DTMF، نگه‌داشتن، ضبط،
انتقال، کنفرانس، پیام فارسی، رجیستر اکانت‌ها، تنظیمات مدیا و کدک، مخاطبین،
تاریخچه، فکس و وضعیت واقعی ۱۲ کلید BLF در دسترس‌اند. هر تماس شناسهٔ خودش را دارد؛
در حالت چندتماسه بهتر است همان شناسه را صریح بفرستی.

برای فکس، اول متن یا فایل را به `/documents` بده؛ PDF، TXT و تصویر را خودش به A4
تبدیل می‌کند. وقتی وضعیت `ready` شد، با شناسهٔ سند به `/faxes` بفرست.
پروتکل پیش‌فرض T.38 است و `g711` هم قابل انتخاب است. برای دریافت، روی شناسهٔ تماس
ورودی فرمان `/calls/ID/fax` بده. نتیجه و فایل فکس دریافتی از `/faxes` قابل دریافت است.

با `/status` وضعیت فعلی و با `/events?since=ID` تغییرها را ببین. پاسخ موفق API یعنی
فرمان پذیرفته شده؛ برای وصل‌شدن تماس، نتیجهٔ ترانسفر یا تحویل فکس، وضعیت نهایی را
هم بررسی کن. تغییر تنظیمات اکانت و شبکه از API زمانی انجام می‌شود که Settings بسته
باشد تا تنظیمات در حال ویرایش کاربر از بین نرود. همهٔ مسیرها توکن و IP مجاز می‌خواهند.
