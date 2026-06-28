# Pisces Moon OS — C5 Edge Radio + LoRa Data Capture — Handoff to The Clinician

**Scope:** Two transports landed in the LilyGO / C5 development arc. This document describes what data each one can gather, the exact on-the-wire / on-disk shapes, and — critically — what is wired into the wardrive database versus what is only reachable by polling and not yet persisted.

**TL;DR for planning:**
- **LoRa → wardrive logging is wired and live.** Every received LoRa frame is GPS-stamped and written to a new `lora_seen` SQLite table.
- **C5 transport is live and commandable, but its data is NOT yet drained into wardrive.** The pop-queues exist and are documented below; nothing currently consumes them into the DB. Treat C5 data as "available to a future consumer," not "in the session DB today."

---

## PART 1 — LoRa data capture (WIRED, LIVE)

### 1.1 Capture mechanics

Wardrive subscribes to the on-board SX1262 via `pm_lora_set_logger_cb()` — a **secondary** callback slot that fires for every received frame, independent of whatever app holds the **primary** RX callback (mesh messenger, voice terminal). This lets wardrive passively log all LoRa traffic while another app is actively using the radio; both callbacks fire per frame. The radio only goes to standby when *both* slots are clear.

LoRa is single-channel by physics. Wardrive only hears traffic on the SX1262's current tuning — there is no "LoRa monitor mode" that hears all bands at once, unlike the C6's 2.4 GHz Wi-Fi promiscuous mode.

| Mode | Tuning | What it captures |
|---|---|---|
| Mesh (default) | Meshtastic LongFast US: 906.875 MHz, SF11, BW250, CR4/8, sync 0x2B | Every Meshtastic node on the US default LongFast channel in range |
| Voice | 906.5 MHz, 2-FSK, 100 kbps, 50 kHz dev | Pisces Voice Terminal carriers (only if user switched to voice mode) |

No preset hopping. **Not captured:** LoRaWAN (different freq plan + encrypted), non-default Meshtastic presets (MediumFast/ShortFast/LongSlow — different SF/BW), and the encrypted Data submessage on non-primary Meshtastic channels (header fields stay visible; payload is AES-CTR ciphertext — we log its hash, not its contents).

### 1.2 `lora_seen` table (19 columns)

`id, node_id, from_id, to_id, pkt_id, port_num, hop_limit, want_ack, rssi, snr_x10, freq_khz, preset, payload_len, payload_hash, text_preview, lat, lng, first_ms, last_ms, hits`

Key fields:
- `node_id` — 8-hex from-node ID (Meshtastic), or `raw{hash}` for frames too short (<16 B) to carry a Meshtastic header
- `from_id` / `to_id` — sender / destination node IDs; `ffffffff` = mesh broadcast
- `pkt_id` — Meshtastic packet ID (dedup key in the mesh)
- `port_num` — Meshtastic portnum, decoded from the payload protobuf when present
- `snr_x10` — SNR × 10 (so `50` → 5.0 dB; stored as int to avoid a float column)
- `freq_khz` — see caveat §1.4.1 — **inferred from mode**, not measured
- `text_preview` — decoded plaintext, populated **only** when `port_num == 1` (TEXT_MESSAGE_APP) on the unencrypted primary channel; empty otherwise
- `payload_hash` — FNV-1a of the full frame; content fingerprint and dedup key
- `hits` — number of times this exact frame was heard (multi-hop receives)

Indices: `idx_lora_node(node_id)`, `idx_lora_last(last_ms)`.

**Upsert rule:** key is `(node_id, payload_hash)`. The same packet heard on multiple hops bumps `hits` and refreshes `rssi`/`snr_x10`/`last_ms`. Distinct packets from the same node create new rows. So you get both per-node coverage (`GROUP BY node_id`) and per-packet hop visibility.

### 1.3 Meshtastic parse depth

The 16-byte header is parsed inline (to / from / id / flags → hop_limit, want_ack). The payload is run through `_mesh_decode_data()`, a minimal protobuf reader that extracts portnum (field 1, varint) and, for portnum 1 only, the text body (field 2, length-delimited; non-printable bytes are scrubbed to `?` so they can't poison the SQLite TEXT column).

POSITION_APP (3), NODEINFO_APP (4), TELEMETRY_APP (67), etc. are logged with `port_num` + `payload_hash` but **not** field-extracted. This is deliberate: those channels are usually encrypted, so the bytes we'd parse would be ciphertext, and full protobuf decode would need a real nanopb dependency.

### 1.4 LoRa open issues (design around these)

1. **`freq_khz` is inferred from mode, not read from the chip.** Filled with `906875` (mesh) or `906500` (voice) by inspecting `pm_lora_current_mode()`. If a future path retunes via `pm_lora_set_freq_mhz()`, the field goes stale. Treat it as "nominal channel," not measured carrier. Real per-frame frequency would need a new `pm_lora_current_freq_khz()` API.
2. **LoRa CSV export is not implemented.** `pm_app_wardrive_export_csv()` dumps only `wifi_seen` today. To feed `lora_seen` to downstream tooling, add a second export to `wardrive_<ts>_lora.csv` — the only design choice is column-name aliasing.
3. **DIO1 RX is polling-mode on LilyGO.** The SX1262 IRQ line is routed through the XL9535 I²C expander (IO17), not a direct GPIO, so frames are pulled on a timer rather than edge-triggered. May miss back-to-back frames in dense traffic; reliable at typical mesh density (<1 pkt/s).
4. **`s_pkt_total` HUD counter is shared with 802.11 packets.** The on-screen PACKETS tile counts both raw 802.11 frames and LoRa frames, so you can't chart LoRa-only rate from the live HUD — read it from the DB.

---

## PART 2 — C5 edge radio (TRANSPORT LIVE; DATA NOT YET PERSISTED)

### 2.1 What the C5 is

An ESP32-C5 "NM-GTD-C5" board running Pisces Moon Edge, connected over **UART2 (GPIO 45/46, 921600 baud)** via the EXT_1X4P_2 header on the LilyGO T-Display-P4. It brings two capabilities the C6 Ghost cannot:

1. **5 GHz Wi-Fi 6** — the C6 is 2.4 GHz only. Capability bit `PM_C5_CAP_WIFI_5GHZ = 1u << 7`.
2. **A second, parallel 2.4 GHz radio** — so the device can sniff and beacon at the same time without blocking the C6's own scan loop.

It speaks the same **PMU1 ASCII protocol** as the Cardputer ADV, so it reuses the Cardputer's PMU1 struct types verbatim rather than carrying a second parser.

Board note: on LilyGO, EXT_1X4P_2 is a clean UART header (no conflict). On the Elecrow boards the same pads (GPIO 45/46) are shared with GT911 touch, so enabling the C5 there trades away touch — the component is available but must be opted into.

### 2.2 Data the C5 can surface

Three inbound pop-queues, each reusing a Cardputer PMU1 struct:

**Wi-Fi frame** — `pm_c5_uart_wifi_frame_pop(pm_cardputer_i2c_wifi_frame_t*)`
Fields: `frame_type, channel, rssi, mac[6], len, data[96]`. Includes 5 GHz channels (36–165) when started via `pm_c5_uart_wifi_promisc_start_5ghz(channel, filter)` — the C5 picks the UNII band from the channel number.

**BLE sighting** — `pm_c5_uart_ble_seen_pop(pm_cardputer_i2c_ble_seen_t*)`
Fields: `mac[18], name[32], rssi, addr_type[24], mfg[24]`.

**LoRa RX** — `pm_c5_uart_lora_rx_pop(pm_cardputer_i2c_lora_rx_t*)`
Fields: `len, rssi, snr_x4` (SNR × 4), `freq_khz, data[180]`. Note this is the C5's *own* LoRa radio, distinct from the P4's on-board SX1262 that Part 1 covers — if both are populated, you have two independent LoRa receivers.

All three return `ESP_OK` with `.available == 0` when the queue is empty.

### 2.3 Commands the C5 accepts

`pm_c5_uart_ble_scan_start(active)`, `pm_c5_uart_ble_scan_stop()`, `pm_c5_uart_wifi_promisc_start(channel, filter)`, `pm_c5_uart_wifi_promisc_start_5ghz(channel, filter)`, `pm_c5_uart_wifi_promisc_stop()`, `pm_c5_uart_wifi_set_channel(channel)`. Also reachable through the peer registry via `pm_c5_uart_call(op, params)` with the same op vocabulary as the Cardputer.

### 2.4 THE GAP — C5 data is not drained into wardrive

**This is the most important line in this document.** As of this build:
- `pm_c5_uart_init()` is called in the service bring-up task, so the UART link comes up and the C5 can HELLO and be commanded.
- **Nothing calls the three `*_pop()` functions to move C5 data into the wardrive DB.** `pm_app_secondary_scan` issues *commands* to a secondary-radio peer but does not persist results.

So C5 Wi-Fi/BLE/LoRa data is **reachable by polling the queues** but is **not logged anywhere** today. There is no `c5_seen` table and no fan-out equivalent to the LoRa logger callback.

To wire C5 into wardrive (future work, mirrors the LoRa pattern):
1. Add a poller in the service task (or a dedicated task) that calls `pm_c5_uart_wifi_frame_pop` / `_ble_seen_pop` / `_lora_rx_pop` on a cadence while wardrive is active.
2. Route Wi-Fi frames to `pm_app_wardrive_on_wifi` / `_on_pkt`, BLE to `pm_app_wardrive_on_ble`, and C5-LoRa to `pm_app_wardrive_on_lora` (the same intake the SX1262 already uses) — or add a dedicated `c5_seen` table if you want to distinguish C5-sourced rows from C6-sourced rows.
3. The 5 GHz Wi-Fi frames are the unique value here — nothing else in the device can see 5 GHz, so a `band` or `is_5ghz` column is worth adding if C5 Wi-Fi gets persisted.

### 2.5 C5 open issues

1. **No persistence (see §2.4).** The headline gap.
2. **`snr_x4` vs `snr_x10`.** The C5 LoRa struct reports SNR × 4; the P4 on-board `lora_seen` table stores × 10. Normalize on ingest if both feed one table.
3. **No GPS stamping yet.** Because nothing drains the queues, no GPS coordinates are attached to C5 data. A future poller should stamp from `pm_gps_state` at pop time, exactly as the LoRa logger does.
4. **HELLO-gated.** `pm_c5_uart_link_seen()` is false until the C5 sends its first PMU1 HELLO; a never-connected C5 correctly appears as no usable peer.

---

## PART 3 — Combined picture: what a single wardrive pass can collect

On the LilyGO T-Display-P4 specifically, the device is uniquely positioned for a multi-band coverage map in one pass:

| Band / source | Radio | Status in wardrive DB |
|---|---|---|
| 2.4 GHz Wi-Fi | C6 (ESP-Hosted) | Wired — `wifi_seen` |
| BLE | C6 (ESP-Hosted) | Wired — `ble_seen` |
| LoRa mesh (Meshtastic) | On-board SX1262 | Wired — `lora_seen` (this work) |
| 5 GHz Wi-Fi | C5 (if attached) | **Not persisted** — pop-queue only (§2.4) |
| Parallel 2.4 GHz Wi-Fi / BLE | C5 (if attached) | **Not persisted** — pop-queue only (§2.4) |
| C5's own LoRa | C5 (if attached) | **Not persisted** — pop-queue only (§2.4) |

So today's session DB gives you C6 Wi-Fi + C6 BLE + on-board LoRa, all GPS-stamped. The C5 lights up the 5 GHz and second-radio dimensions, but only once a queue-drainer is added.

---

## PART 4 — Suggested analyses (for whatever the Clinician builds)

- **LoRa mesh coverage map:** `SELECT node_id, lat, lng, MIN(first_ms), MAX(last_ms), COUNT(*) FROM lora_seen WHERE port_num != 0 GROUP BY node_id` — plot each node at its mean GPS position weighted by RSSI.
- **LoRa chat capture:** `SELECT first_ms, from_id, text_preview FROM lora_seen WHERE port_num = 1 AND text_preview != '' ORDER BY first_ms` — plaintext primary-channel only.
- **Unique mesh nodes over time:** bucket `first_ms / 60000`, `COUNT(DISTINCT node_id)` per bucket.
- **Cross-band correlation:** a node in both `lora_seen` and `ble_seen` with overlapping GPS is interesting — many Meshtastic devices expose their NodeID in the BLE name. Heuristic: `lora_seen.from_id` substring in `ble_seen.name`.

---

## PART 5 — File / symbol reference

- LoRa schema: `components/pm_apps/pm_apps_cyber/include/pm_wardrive_schema.h` (`lora_seen`)
- LoRa intake: `components/pm_apps/pm_apps_cyber/pm_app_wardrive.c` → `pm_app_wardrive_on_lora()`, `_mesh_decode_data()`
- LoRa RX→wardrive trampoline: `main/main.c` → `_lora_to_wardrive()`
- LoRa dual-callback API: `components/pm_lora/include/pm_lora.h` → `pm_lora_set_rx_cb()` (primary) + `pm_lora_set_logger_cb()` (secondary; wardrive uses this)
- C5 transport + data API: `components/pm_c5_uart/include/pm_c5_uart.h`
- C5 / Cardputer shared structs: `components/pm_cardputer_i2c/include/pm_cardputer_i2c.h` (`pm_cardputer_i2c_wifi_frame_t`, `_ble_seen_t`, `_lora_rx_t`)
- C5 init point: `main/main.c` service bring-up task → `pm_c5_uart_init()` (no queue-drainer wired — see §2.4)
