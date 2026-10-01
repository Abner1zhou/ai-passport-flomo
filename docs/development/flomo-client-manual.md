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
> Device-side behavior (recording quality, hotspot provisioning and the
> configuration page, transcription accuracy, flomo writes, Chinese rendering)
> is not yet verified on hardware. This manual describes the implemented
> behavior.

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
- A phone with Wi-Fi and a browser: it joins the device hotspot for
  provisioning and the same network for later edits. No app is required.

Provisioning and the follow-up configuration both happen on the device's own
web page; no WeChat mini program is involved.

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
| Provisioning | Settings → Start provisioning → OK | Provisioning status, hotspot name `FoloPassport-XXXX`, page address `192.168.4.1`; *Connected* with the IP after success | Click OK to exit |

The Recording screen actually accepts a click of **either** UP or DOWN to
discard (the footer hint only mentions UP).

## Button reference

| Screen | Click UP | Click DOWN | Click OK | Long-press OK | Long-press UP |
| --- | --- | --- | --- | --- | --- |
| Home | Previous page | Next page | — | Start recording | — |
| Recording | Discard memo | Discard memo | Finish memo | — | — |
| Progress | Ignored | Ignored | Ignored | Ignored | Ignored |
| Queue | — | — | Retry sync now | Delete oldest memo | Back to Home |
| Settings | Move selection | Move selection | Run selected item | — | Back to Home |
| Provisioning | — | — | Exit provisioning | — | — |

Both UP and DOWN move the Settings selection forward through the three items;
with three entries, at most two presses reach any item.

## First-time setup

### 1. Power on

The home screen shows the Wi-Fi status *Not configured* and `0` queued memos.

### 2. Start the device hotspot

1. From home, click DOWN twice to open **Settings**.
2. Click UP or DOWN until `>` marks **Start provisioning**, then click OK.
3. The device opens an **open hotspot** (no password) named like
   `FoloPassport-A1B2` (the last four hex digits differ per device). The
   provisioning screen shows the hotspot name and the page address
   `192.168.4.1`.

### 3. Join the hotspot from the phone

In the phone's Wi-Fi settings pick `FoloPassport-XXXX`. The phone may warn that
the network "has no Internet" or suggest switching to mobile data — **stay
connected and do not switch**; provisioning only needs the local link between
phone and device.

### 4. Provision and configure on the web page

Open `http://192.168.4.1/` in the phone's browser. The page has two parts:

- **Wi-Fi**: a dropdown with the networks the device scanned (with signal
  strength and encryption state); picking one fills the network name, and
  hidden networks can be typed manually. Below it, the Wi-Fi password field
  (leave empty for open networks).
- **flomo/ASR configuration**: five fields —

| Field | Meaning | Rule |
| --- | --- | --- |
| flomo Webhook | The Incoming Webhook URL copied from flomo | Required; must start with `https://` |
| ASR endpoint URL | OpenAI-compatible transcription endpoint | Must start with `https://`; defaults to Groq |
| ASR Key | API key of the transcription service | Required; typed as a password |
| ASR model | Model name | Defaults to `whisper-large-v3` |
| Language | Transcription language hint such as `zh` | Defaults to `zh`; empty means auto-detect |

Press **Save & connect**: the device saves the configuration, then connects
with the submitted Wi-Fi credentials. The page polls the result and shows
*Connected \<ip\>* or *Connection failed*.

> [!IMPORTANT]
> The ASR key is write-only: it is stored on the device but never echoed back
> to the page. **Leaving the key field empty keeps the stored key unchanged**;
> on first setup you must fill it in, otherwise transcription cannot work.

### 5. Done

On success the device screen shows *Connected* with the IP and, about 5 s
later, closes the hotspot and returns to home (the short delay lets the phone
page poll the result). Wi-Fi credentials and the flomo/ASR configuration are
saved; after a reboot the device reconnects and syncs on its own.

Wrong password, a 5 GHz network, or a weak signal end in *Connection failed* —
the hotspot stays up; correct the form and press Save & connect again. Click
OK to leave provisioning at any time (existing credentials reconnect).

### 6. Record a test memo

Long-press OK on home, say a sentence, click OK. The screen runs through
*Transcribing…* to *Sent*, and the note appears in flomo within seconds: its
content is the raw transcript; no tags are added.

## Daily use

### Changing configuration or Wi-Fi

- **Edit the flomo/ASR configuration**: once the device is online, open
  `http://<device-ip>/` from a browser on the same network (the device never
  shows its IP on the home screen; find it in the router's DHCP list, or note
  the address the provisioning page displayed on success). Same page, without
  the Wi-Fi section. A static DHCP lease (IP reservation) avoids hunting for
  the address later.
- **Switch Wi-Fi networks**: re-enter **Settings → Start provisioning** and
  repeat steps 3–5 of first-time setup. The stored flomo/ASR configuration is
  untouched (an empty key field keeps the stored key).

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
| Page shows *Connection failed* | Wrong Wi-Fi password, 5 GHz network, or weak signal | The hotspot stays up; correct the form and save again |
| Phone cannot find the `FoloPassport-XXXX` hotspot | The device is not on the provisioning screen, or is too far away | Make sure the device stays on the provisioning screen; move closer |
| Hotspot connected but `192.168.4.1` does not load | The phone switched to mobile data / another Wi-Fi, or the browser enforced https | Stay on the device hotspot; type `http://` (not https) manually |
| Page shows *Save failed* | Webhook or endpoint URL does not start with `https://`, or a value is too long | Correct the value and save again |
| Provisioning screen stuck after *Connection failed* | A connection attempt failed and the device is waiting for the next submit from the page | Correct and resubmit on the page; or click OK to exit and re-enter |
| Connection failed on home | Saved credentials do not work or the network is unreachable; the device keeps retrying | Check the router; re-provision to switch networks |
| Failed-queued after recording | A network or server error occurred during transcription or posting | Nothing to do: retried automatically about once a minute |
| Queue count drops but no note appears in flomo | A memo was dropped: HTTP 4xx (webhook token invalid or expired), 5 failed attempts, or a transcript over 3 KB | Check the webhook URL in flomo; prefer shorter memos |
| A recording ends and is discarded immediately | The queue partition is full | Sync or delete queued memos, then record again |
| Screen is dark | Idle dimming after 15 s | Press any button |
| Device does not wake after Power off | Deep sleep has no button wake | Use the reset button or re-power |
| Home still shows Not configured after reboot | Configuration was cleared | Redo [First-time setup](#first-time-setup) |

## Privacy and data

- Wi-Fi credentials and the flomo/ASR configuration are stored in the device's
  NVS. The ASR key is write-only: never echoed to the configuration page and
  never written to logs; page request bodies are likewise never logged.
- The provisioning hotspot is an **open network** served over plain HTTP:
  while provisioning is active, anyone nearby can connect and submit settings
  (including the Wi-Fi password you type). This is the accepted trade-off of
  SoftAP provisioning (the same as xiaozhi-style devices): provision in a
  trusted environment; the hotspot closes as soon as the device connects.
- The post-connection configuration page also uses plain HTTP on the LAN — a
  deliberate trade-off, since the device IP is dynamic and a server
  certificate cannot be validated. Use the page only on networks you trust.
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
| Provisioning hotspot | `FoloPassport-XXXX`, open; page at `http://192.168.4.1/` |
| flomo | Incoming Webhook requires flomo PRO |
