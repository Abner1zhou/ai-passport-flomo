<p align="right">
  <a href="flomo-client.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# flomo Voice-Memo Client

A derivative application (branch `feature/flomo-client`) that records voice memos with
the OK button, transcribes them through a cloud ASR endpoint, and writes the text to
flomo through its Incoming Webhook. Offline memos are queued in a littlefs partition
and synced automatically.

## Requirements

- **flomo PRO**: the Incoming Webhook (`https://flomoapp.com/iwh/<token>`) is a PRO
  feature. flomo has unpublished rate limits; the client throttles retries
  (60 s base backoff on HTTP 429, capped at 600 s).
- An OpenAI-compatible transcription endpoint and API key. Defaults target
  `https://api.groq.com/openai/v1/audio/transcriptions` with `whisper-large-v3`;
  any compatible endpoint works.

## First-time setup

1. Power on and press OK (DOWN cycles pages) to reach **Settings**.
2. Select **Start provisioning**. The device advertises over BLE as
   `BLUFI_FoloPassport`. Use the companion WeChat mini program (exact name:
   [see the provisioning guide](engineering/wifi-provisioning.md#mini-program-name))
   to send 2.4 GHz Wi-Fi credentials.
3. After the device connects, open `http://<device-ip>/` from a browser on the same
   network and fill in the flomo webhook URL, ASR endpoint, key, model, and language.
   The ASR key is stored in NVS only, never echoed back, and never logged.
   Plain HTTP on the LAN is a deliberate trade-off: the device IP is dynamic, so a
   server certificate cannot be validated.
4. **Clear configuration** erases the flomo/ASR config and saved Wi-Fi
   credentials.

## Daily use

- **Record**: long-press OK on the home screen; click OK to finish, click UP/DOWN to
  discard. Maximum 120 s per memo (16 kHz/16-bit mono WAV, about 1.9 MB/min).
- **Sync**: after recording, the device transcribes and posts automatically. On
  failure the memo stays queued; the **Queue** screen shows pending count, the
  oldest memo's duration and retry count. OK re-triggers a sync, long-press OK
  deletes the oldest memo.
- A memo is dropped after 5 failed attempts (immediately on HTTP 4xx, e.g. an invalid
  webhook token).
- The battery percentage is shown in the top-right corner; the backlight dims after
  15 s of inactivity. **Power off** enters deep sleep — the board has no
  validated button-wake circuit, so waking requires the reset button or re-power.

## Partition layout

`partitions.csv` on this branch reserves a ~4.8 MB `storage` (littlefs) partition for
the offline queue — about 2.5 minutes of 16 kHz/16-bit mono audio in total — and
shrinks the factory app to ~3.06 MB. Flashing the merged image reformats the previous
single-app layout.

## Validation status

Host tests cover the WAV header, JSON parsing/escaping, config parsing, queue retry
policy, and UI key mapping; a repository test enforces that every Chinese literal on
the display surface is covered by the generated font subset. On-device behavior
(recording quality, BluFi pairing with the mini program, ASR accuracy, flomo writes,
Chinese rendering) is listed as unverified until hardware testing is performed.
