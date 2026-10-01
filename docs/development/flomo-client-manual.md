<p align="right">
  <a href="flomo-client-manual.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# flomo Voice-Memo Client Operation Manual

Step-by-step instructions for using the flomo voice-memo application on the AI
Passport device (firmware on branch `feature/flomo-client`): hold one button to
record a voice memo, the device transcribes it through a cloud service and
writes the text to flomo. Memos recorded without network access are queued on
the device and synced automatically later.

The on-device UI is Chinese; the screen names below are English descriptions of
the Chinese labels. For build, partition, and validation background see the
[developer notes](flomo-client.md).

> [!NOTE]
> Device-side behavior (recording quality, provisioning with the mini program,
> transcription accuracy, flomo writes, Chinese rendering) is not yet verified
> on hardware. This manual describes the implemented behavior.

## What you need

- An AI Passport device flashed with the flomo client firmware.
- A **flomo PRO** account with an Incoming Webhook URL
  (`https://flomoapp.com/iwh/<token>`). Create the webhook in flomo's settings
  and copy the full URL; webhooks are a PRO feature.
- A transcription service: any OpenAI-compatible `/audio/transcriptions`
  endpoint and its API key. The defaults target Groq
  (`https://api.groq.com/openai/v1/audio/transcriptions`,
  model `whisper-large-v3`); any compatible endpoint works.
- A **2.4 GHz** Wi-Fi network. 5 GHz is not supported.
- WeChat on your phone, for the provisioning mini program.
- A phone or computer with a browser on the same network, for the device
  configuration page.

## The device at a glance

Three buttons control everything: **UP**, **DOWN**, and **OK**.

- A **click** means press and release. A **long press** means hold for about
  1.5 s.
- The battery percentage sits in the top-right corner. Below 15 % the home
  screen adds a low-battery warning.
- The backlight dims after 15 s without input. Any button restores it; that
  press also performs its normal action.
- **Power off** (last item of Settings) enters deep sleep. The board has no
  validated button-wake circuit: waking requires the reset button or
  re-powering.

## Screens

Six screens; from home, click DOWN to walk forward (Home → Queue → Settings)
and UP to walk back. From Queue or Settings, a long press of UP always returns
to home. On Settings, UP/DOWN clicks move the selection instead of changing
pages.

| Screen | How to get there | What it shows | Footer hint |
| --- | --- | --- | --- |
| Home | Startup screen; return target of every back action | Wi-Fi status (Not configured / Provisioning / Connecting / Connected / Connection failed), queued-memo count, low-battery warning | Hold OK to record |
| Recording | From Home, long-press OK | Elapsed time `mm:ss`, input level percentage | Click OK to finish; UP or DOWN to discard |
| Progress | Automatic, after a recording finishes | Transcribing…, then Sent or Failed-queued for about 3 s | Keys are ignored |
| Queue | From Home, click DOWN | Pending count, oldest memo's duration `mm:ss`, its attempts `n/5` | Click OK to retry; long-press OK to delete |
| Settings | From Queue, click DOWN | Selection list: Start provisioning / Clear configuration / Power off | Long-press UP for back |
| Provisioning | Settings → Start provisioning → OK | Provisioning status and the SSID; Connection failed on error | Click OK to abort |

## Button reference

| Screen | Click UP | Click DOWN | Click OK | Long-press OK | Long-press UP |
| --- | --- | --- | --- | --- | --- |
| Home | Previous page | Next page | — | Start recording | — |
| Recording | Discard memo | Discard memo | Finish memo | — | — |
| Progress | Ignored | Ignored | Ignored | Ignored | Ignored |
| Queue | — | — | Retry sync now | Delete oldest memo | Back to Home |
| Settings | Move selection | Move selection | Run selected item | — | Back to Home |
| Provisioning | — | — | Abort provisioning | — | — |

Both UP and DOWN move the Settings selection forward through the three items;
with three entries, at most two presses reach any item.

## First-time setup

### 1. Power on

The home screen shows the Wi-Fi status *Not configured* and `0` queued memos.

### 2. Connect Wi-Fi (Bluetooth provisioning)

1. From home, click DOWN twice to open **Settings**.
2. Click UP or DOWN until `>` marks **Start provisioning**, then click OK. The
   device advertises over Bluetooth LE as `BLUFI_FoloPassport`.
3. On the phone: enable Bluetooth and the required permissions, open WeChat,
   search for the companion mini program (exact name:
   [see the provisioning guide](engineering/wifi-provisioning.md#mini-program-name)),
   select the device, and send the credentials of a 2.4 GHz network.
4. The provisioning screen shows *Provisioning…* and then *Connected* or
   *Connection failed*. On success the device returns to home and the status
   shows *Connected*; the credentials are saved and reused after every reboot.
   Click OK to abort provisioning and return home.

Wrong password, a 5 GHz network, or being out of range end in
*Connection failed*; simply retry.

### 3. Find the device IP address

The device never displays its IP on screen after provisioning. Use one of:

- **Serial console**: connect the device over USB and watch the console; when
  Wi-Fi connects, ESP-IDF prints a `sta ip: x.x.x.x` line.
- **Router**: look up the device in the router's DHCP client list.

The configuration page address follows the IP, so a static DHCP lease (IP
reservation) for the device avoids hunting for the address later.

### 4. Fill in the configuration page

Open `http://<device-ip>/` from a browser on the same network. The page shows
the pending queue count and five fields:

| Field | Meaning | Rule |
| --- | --- | --- |
| flomo Webhook | The Incoming Webhook URL copied from flomo | Required; must start with `https://` |
| ASR endpoint URL | OpenAI-compatible transcription endpoint | Must start with `https://`; defaults to Groq |
| ASR Key | API key of the transcription service | Required; typed as a password |
| ASR model | Model name | Defaults to `whisper-large-v3` |
| Language | Transcription language hint such as `zh` | Defaults to `zh`; empty means auto-detect |

Click **Save**. The page answers *Save succeeded* or *Save failed* — a failure
means a value is invalid (webhook or endpoint not starting with `https://`) or
too long; nothing is changed and no partial data is written.

> [!IMPORTANT]
> The ASR key is write-only: it is stored on the device but never echoed back
> to the page. **Re-enter the ASR key on every later save**, even when changing
> an unrelated field — submitting the form with an empty key field clears the
> stored key and syncing stops until the key is entered again.

### 5. Record a test memo

Long-press OK on home, say a sentence, click OK. The screen runs through
*Transcribing…* to *Sent*, and the note appears in flomo within seconds: its
content is the raw transcript; no tags are added.

## Daily use

### Recording a memo

1. On home, long-press OK. Recording starts immediately; the screen shows the
   elapsed time and input level.
2. Click OK to finish, or click UP/DOWN to discard the recording. A memo also
   ends automatically at 120 s.
3. After finishing, the progress screen shows *Transcribing…*, then *Sent* or
   *Failed-queued* for about 3 s, then returns to home. While offline or
   unconfigured, the device returns to home after about 3 s and keeps the memo
   queued.

### The offline queue

- The Queue screen shows the pending count, the oldest memo's duration, and
   its attempt count `n/5`.
- Syncing runs automatically: right after a recording, when Wi-Fi connects,
   after saving the configuration page, and about once a minute while the
   queue is non-empty.
- A failed memo is retried automatically about once a minute. After 5 failed
   attempts in total the memo is dropped and deleted; an HTTP 4xx response
   (for example an invalid or expired webhook token) drops the memo
   immediately. Dropped memos cannot be recovered.
- Queue screen actions: click OK to retry the sync immediately; long-press OK
   to delete the oldest memo; long-press UP to return home.
- Capacity: the queue partition holds about 5 MB, roughly 2.5 minutes of
   audio in total. When it is full, a newly started recording is cancelled
   automatically — sync or delete memos, then record again.

### Power off

Settings → **Power off** enters deep sleep. Wake the device with the reset
button or by re-powering. Queued memos, Wi-Fi credentials, and the flomo/ASR
configuration all survive.

## Status meanings and troubleshooting

| What you see | Meaning | What to do |
| --- | --- | --- |
| Save failed on the configuration page | Webhook or endpoint URL does not start with `https://`, or a value is too long | Correct the value and save again |
| Memos stopped syncing after a later configuration save | The form was saved with an empty ASR key, which cleared the stored key | Re-enter the key and save |
| Connection failed on home | Saved credentials do not work or the network is unreachable; the device keeps retrying | Check the router; re-provision to switch networks |
| Failed-queued after recording | A network or server error occurred during transcription or posting | Nothing to do: retried automatically about once a minute |
| Queue count drops but no note appears in flomo | A memo was dropped: HTTP 4xx (webhook token invalid or expired), 5 failed attempts, or a transcript over 3 KB | Check the webhook URL in flomo; prefer shorter memos |
| A recording ends and is discarded immediately | The queue partition is full | Sync or delete queued memos, then record again |
| Screen is dark | Idle dimming after 15 s | Press any button |
| Device does not wake after Power off | Deep sleep has no button wake | Use the reset button or re-power |
| Provisioning ends in Connection failed | Wrong password, 5 GHz network, or out of range | Retry with correct 2.4 GHz credentials |
| Home still shows Not configured after reboot | Configuration was cleared | Redo [First-time setup](#first-time-setup) |

## Privacy and data

- Wi-Fi credentials and the flomo/ASR configuration are stored in the device's
  NVS. The ASR key is write-only: never echoed to the configuration page and
  never written to logs.
- The configuration page uses plain HTTP on the LAN. This is a deliberate
  trade-off: the device IP is dynamic, so a server certificate cannot be
  validated. Use the page only on networks you trust.
- Voice memos are stored on device flash until they are synced or dropped,
  then deleted. Audio is uploaded to the configured ASR endpoint; the
  transcript is posted to flomo's webhook.
- **Clear configuration** (Settings) erases the flomo/ASR configuration and
  the saved Wi-Fi credentials. It does not delete queued audio; the device is
  then unconfigured and must be set up again.

## Limits

| Limit | Value |
| --- | --- |
| Memo length | 120 s maximum, then auto-finish |
| Recording format | 16 kHz / 16-bit mono WAV, about 1.9 MB per minute |
| Offline queue | About 5 MB in total, roughly 2.5 minutes of audio |
| Transcript length | 3 KB maximum; longer transcripts are dropped |
| Upload attempts | 5 per memo; HTTP 4xx drops immediately |
| Wi-Fi | 2.4 GHz only |
| flomo | Incoming Webhook requires flomo PRO |
