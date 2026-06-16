# Pisces Moon P4 — App Redesign Catalog

**Reference:** `/Users/eric/Documents/GitHub/pisces-moon-linux/html/`
**Goal:** Move every P4 app away from "320×240 scaled up" toward proper layouts that exploit the 1024×600 (7") and 800×480 (5") canvases.

## Design system anchor

All P4 LVGL color tokens already match the canonical `pm_theme.css` palette:

| Token (P4) | HTML var | Hex |
|---|---|---|
| `PM_LAYOUT_COL_BG` | `--bg` | `#060d14` |
| `PM_LAYOUT_COL_BG2` | `--bg2` | `#0a1520` |
| `PM_LAYOUT_COL_BG3` | `--bg3` | `#0f1e2c` |
| `PM_LAYOUT_COL_BORDER` | `--panel-border` | `#1f4060` |
| `PM_LAYOUT_COL_ACCENT` | `--accent` | `#4dd9ff` |
| `PM_LAYOUT_COL_OK` | `--accent3` | `#4dffa6` |
| `PM_LAYOUT_COL_GOLD` | `--gold` | `#ffd166` |
| `PM_LAYOUT_COL_WARN` | `--warn` | `#ffe066` |
| `PM_LAYOUT_COL_ERR` | `--accent4` | `#ff5577` |
| `PM_LAYOUT_COL_PURPLE` | `--purple` | `#c89eff` |
| `PM_LAYOUT_COL_FG_BR` | `--text-bright` | `#ffffff` |
| `PM_LAYOUT_COL_FG` | `--text` | `#c8e6f5` |
| `PM_LAYOUT_COL_FG_DIM` | `--text-mid` | `#8db8d0` |
| `PM_LAYOUT_COL_DIM` | `--text-dim` | `#5a8aa4` |

## New helpers in `pm_app_layout`

The HTML CSS reveals four reusable patterns. P4 layout helpers added this phase:

- `pm_app_layout_section_header(parent, "DEVICES", "24")` — slim Orbitron-style strip with optional accent count
- `pm_app_layout_chart_section(parent, "VENDOR BREAKDOWN")` — bordered group with gold uppercase title
- `pm_app_layout_item_row(parent, primary, meta, selected)` — list row with selection accent (2px left border + tinted bg)
- `pm_app_layout_stat_color(value_label, COL)` — color-tint stat values green/gold/red after creation

These are additive; existing apps keep working without modification.

## Layout pattern catalog

The HTML mockups settle on three structural patterns. The right pattern for an app depends on whether it's data-dense or text-heavy.

### Pattern A — Three-column cyber dashboard

300px LEFT (list) | flex CENTER (canvas/map/viz) | 300px RIGHT (stacked chart sections)
Stats row spans full width above. Header at top, action strip inside CENTER pane.

Used by: wardrive, beacon_spotter, bt_radar, probe_intel, pkt_sniffer, pkt_analysis, net_scanner, port_scanner, ble_gatt, ble_ducky, wifi_ducky, usb_ducky, rf_spectrum, silas_creek_parkway, gemini_log.

### Pattern B — Two-column text/form

185-280px LEFT (item/file list) | flex CENTER (editor / detail / form)
Optional toolbar row above center. Optional tabs row above list.

Used by: notepad, contacts, vault, field_notes, recipes, habits, ereader, gemini_terminal, voice_terminal, ssh_client, mesh_messenger.

### Pattern C — Single full-canvas viz

Header + optional stats strip + full-width centre canvas + optional action bar.

Used by: gps_app, compass, sun_moon, tides, weather, sos_beacon, offline_maps, trails, chess, tetris, snake, pacman, galaga, breakout, simcity, etch, calculator, clock, calendar.

## Per-app catalog

Format: `app_id` | HTML ref | Pattern | Current P4 state | Priority

### CYBER

| App | HTML ref | Pattern | Current | Priority |
|---|---|---|---|---|
| wardrive | `wardrive.html` | A | Phase 16 redesign WIP (`_p4_redesign.c` exists), needs charts + map + waterfall | **HIGH** |
| beacon | `beacon_spotter.html` | A | Phase 17 single-pane; needs proper 3-col + chart sections | **HIGH** |
| bt_radar | `bt_radar.html` | A | Phase 17 has 2-col polar+spark; HTML uses 3-col bubble+timeline charts | MEDIUM |
| net_scanner | `net_scanner.html` | A | Likely 320-scaled | HIGH |
| pkt_sniffer | `pkt_sniffer.html` | A | Likely 320-scaled | HIGH |
| pkt_analysis | `pkt_analysis.html` | A | Likely 320-scaled | MEDIUM |
| probe_intel | `probe_intel.html` | A | Likely 320-scaled | MEDIUM |
| port_scanner | `port_scanner.html` | A | Not yet ported | LOW |
| rf_spectrum | `rf_spectrum.html` | A | Likely 320-scaled | MEDIUM |
| hash_tool | `hash_tool.html` | B | Likely 320-scaled | LOW |
| ble_gatt | `ble_gatt.html` | A | Likely 320-scaled | MEDIUM |
| ble_ducky | `ble_ducky.html` | A | Likely 320-scaled | LOW |
| usb_ducky | `usb_ducky.html` | A | Likely 320-scaled | LOW |
| wifi_ducky | `wifi_ducky.html` | A | Likely 320-scaled | LOW |
| wpa_hs | `wpa_handshake.html` | A | Likely 320-scaled | MEDIUM |
| tracker_scan | `bt_radar.html` (similar) | A | Phase 17 single-pane | MEDIUM |
| wardrive_inspect | (none — P4-only) | C | Phase 17 single-pane | OK as-is |
| silas_creek | `silas_creek_parkway.html` | A (flagship!) | P4-only, needs flagship treatment | **HIGH** |
| clinician | (P4-only) | A | P4-only | OK as-is |

### COMMS

| App | HTML ref | Pattern | Current | Priority |
|---|---|---|---|---|
| wifi | `wifi_app.html` | A | Likely 320-scaled | HIGH |
| bluetooth | (bt_radar.html basis) | A | Likely 320-scaled | MEDIUM |
| gps | `gps_app.html` | C | Likely 320-scaled | HIGH |
| mesh_messenger | `mesh_messenger.html` | B | Likely 320-scaled | HIGH |
| voice_terminal | `voice_terminal.html` | B | Likely 320-scaled | MEDIUM |
| lora_voice | (custom) | C | Custom layout | LOW |
| weather | `weather.html` | C | Phase 17 fresh | OK as-is |

### INTEL

| App | HTML ref | Pattern | Current | Priority |
|---|---|---|---|---|
| terminal | (`gemini_terminal.html` basis) | B | Likely 320-scaled | MEDIUM |
| ssh | `ssh_client.html` | B | Likely 320-scaled | MEDIUM |
| gemini_log | `gemini_log.html` | A | Likely 320-scaled | MEDIUM |
| rss | (`news_*.html` basis) | B | Phase 17 fresh | OK as-is |
| trails | `trails.html` | C (with map) | Likely 320-scaled | MEDIUM |
| baseball | `baseball.html` | C | Likely 320-scaled | LOW |
| ref_med | `medical_ref.html` | B | Likely 320-scaled | MEDIUM |
| ref_surv | `survival_ref.html` | B | Likely 320-scaled | MEDIUM |

### TOOLS

| App | HTML ref | Pattern | Current | Priority |
|---|---|---|---|---|
| notepad | `notepad.html` | B (tabs + toolbar + split editor) | Likely 320-scaled | **HIGH** |
| calculator | `calculator.html` | C | Likely 320-scaled | LOW |
| clock | `clock.html` | C | Likely 320-scaled | LOW |
| calendar | `calendar.html` | B | Likely 320-scaled | MEDIUM |
| etch | `etch.html` | C | Likely 320-scaled | LOW |
| keytest | (P4-only) | C | P4-only | OK as-is |
| camera_qr | `qr_tool.html` | C | Phase 15 fresh | OK as-is |
| ereader | (none) | B | Phase 17 fresh | OK as-is |
| contacts | `contacts.html` | B | Phase 17 fresh | LOW review |

### MEDIA

| App | HTML ref | Pattern | Current | Priority |
|---|---|---|---|---|
| audio_player | `audio_player.html` | C | Likely 320-scaled | MEDIUM |
| audio_recorder | `audio_recorder.html` | C | Likely 320-scaled | MEDIUM |
| camera | (custom) | C | Phase 15 fresh | OK as-is |

### SYSTEM

| App | HTML ref | Pattern | Current | Priority |
|---|---|---|---|---|
| files | `filesystem.html` | B | Likely 320-scaled | MEDIUM |
| filemgr | (none — WiFi HTTP server) | C | P4-only | OK as-is |
| about | `about.html` | C | Likely 320-scaled | LOW |
| system | `system_info.html` | C | Likely 320-scaled | LOW |
| bridge | (P4-only) | C | P4-only | OK as-is |
| micropython | (P4-only REPL) | B | P4-only | OK as-is |
| elf_browser | (P4-only) | B | P4-only | OK as-is |
| gamepad | (P4-only) | C | P4-only | OK as-is |

### GAMES

Stubs already match a fixed "PORT IN PROGRESS" template (PCB bg + rainbow title). Real ports will follow each HTML reference (`tetris.html`, `chess.html`, etc.) as separate work. Not priority.

## Suggested redraw order

For maximum perceived improvement per session:

1. **wardrive** — flagship cyber app, anchor for the 3-col pattern; sets the bar for the other Pattern A apps
2. **silas_creek_parkway** — the headline flagship app from the HTML suite
3. **wifi** — most-opened comms app, biggest visual win
4. **beacon** — already Phase 17 fresh, just needs the chart sections + section headers
5. **net_scanner** — common cyber task, easy 3-col port
6. **gps** — main field-ops app, C pattern with rich map+stats
7. **notepad** — most-opened tools app, B pattern with tabs+toolbar+split
8. **mesh_messenger** — main comms use case
9. **gemini_log**, **trails**, **ref_med**, **ref_surv** — medium-effort, high-value
10. Everything else as time allows

## Things to NOT mirror from HTML

- **Canvas-based bubble scatter / waterfall** in bt_radar.html / wardrive.html — LVGL has `lv_chart` and `lv_line` that hit the same UI goal without manual canvas drawing.
- **CSS `backdrop-filter: blur` glass effects** — expensive on the P4 framebuffer. Use solid `--bg2`/`--bg3` instead; the design still reads cyberpunk.
- **`@keyframes pulse` animation on live badge** — use a simple LV_ANIM on opacity if needed, or drop the pulse entirely (the chip color is enough).
- **Sticky positioned section headers** — LVGL doesn't have CSS `position: sticky`, but apps rarely need it because pane scrolls separately from page.

## Things to make sure ARE mirrored

- **Stats row with vertical dividers** between cells (border-right)
- **Color-coded stat values** (NETWORKS green, OPEN red, STRONGEST gold, GPS dim-or-bright)
- **Section header letter-spacing of 2** (Orbitron uppercase look)
- **Selection accent on list items** (2px left border in `--accent`)
- **Right-pane chart section dividers** (border-bottom between stacked charts)
- **Header height of 38-44px** with logo + status chips right-aligned
- **Action buttons as outlined**, not filled (border in color, transparent bg)
