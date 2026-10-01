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

## First-time setup (SoftAP web provisioning, xiaozhi style)

1. Power on, walk to **Settings**, select **Start provisioning**: the device opens
   an open hotspot `FoloPassport-XXXX` (last four hex digits from the MAC); the
   provisioning screen shows the hotspot name and `192.168.4.1`.
2. The phone joins the hotspot and opens `http://192.168.4.1/`: pick a scanned
   network (or type one), enter the password, fill in the flomo webhook / ASR
   fields, press *Save & connect*.
3. The device saves the configuration (`flomo_config` partial-update semantics),
   applies the Wi-Fi credentials, and the page polls `/status.json` for the
   outcome; on success the hotspot stays up for about 5 s before closing and the
   device returns home.
4. Once online, `http://<device-ip>/` is the maintenance entry to the same page
   (without the Wi-Fi section). The ASR key is stored in NVS only, never echoed
   back, and never logged; submitting with an empty key field keeps the stored
   value.
5. **Clear configuration** erases the flomo/ASR config and saved Wi-Fi
   credentials.

Architecture notes: `flomo_wifi` is the single owner of the Wi-Fi stack; the STA
and AP netifs are both created before `esp_wifi_start()`. Provisioning switches
to `WIFI_MODE_APSTA` and suspends the STA auto-reconnect (saved credentials must
not reconnect in the background and interrupt a network change); leaving
provisioning returns to STA-only and reconnects saved credentials when not yet
connected. `esp_http_server` (`flomo_websrv`) serves both the provisioning entry
(192.168.4.1) and the maintenance entry (device IP), with configuration
application unified behind the `apply_config` hook. Provisioning does not use
Bluetooth: the BluFi/NimBLE code and sdkconfig blocks were removed, freeing
RAM/flash and restoring the mbedtls hardware MPI acceleration for TLS.

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
(recording quality, SoftAP provisioning and page interaction, ASR accuracy, flomo
writes, Chinese rendering) is listed as unverified until hardware testing is
performed.
