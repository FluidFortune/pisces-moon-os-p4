<!--
Pisces Moon OS
Copyright (C) 2026 Eric Becker / Fluid Fortune
SPDX-License-Identifier: AGPL-3.0-or-later
Contributions: see CLA.md
fluidfortune.com
-->

# Meshtastic Wardrive Mapping

**Purpose:** capture the design notes for mapping LoRa/Meshtastic node
activity into the existing Pisces Moon wardrive dataset.

This is a passive-first survey mode. The goal is not to force unknown
radios to identify themselves. The goal is to observe LoRa/Meshtastic
traffic while the device is moving, tag every observation with GPS and
radio metadata, and correlate those observations with the existing
WiFi/BLE wardrive database.

## Core model

Meshtastic can show many nodes because most participating nodes are not
silent. They periodically emit or forward traffic such as NodeInfo,
position, telemetry, routing, messages, traceroute, acknowledgements,
and rebroadcast packets. A receiver can often learn that a node exists
from packet headers and routing metadata even when it cannot decrypt or
interpret the payload.

A truly silent LoRa node remains effectively invisible at useful
wardrive range. The mapper should therefore present sightings as
"recently observed mesh activity", not as a complete inventory of all
hardware in the city.

## Passive collection

Each LoRa packet sighting should be logged as a first-class wardrive
event. Store the receiver position and RF conditions at the moment the
packet was heard:

- timestamp in milliseconds
- receiver latitude, longitude, altitude, satellite count, and GPS age
- channel preset or frequency
- spreading factor, bandwidth, coding rate, sync word, and power profile
- RSSI, SNR, packet length, and receive error state
- decoded payload type when available
- origin node ID when visible
- sender/transmitter candidate when visible
- destination node ID when visible
- hop limit, hop start, next hop, relay node, and want-ack flags when visible
- decryption state: decoded, header-only, encrypted, malformed, unknown
- observation source: passive, self-initiated, response-to-self, relay-of-self

Keep the origin node and the RF transmitter candidate separate. In a
mesh, the packet origin may be node A while the physical transmitter
heard by Pisces Moon may be node B rebroadcasting A's packet. Treating
those as the same thing will make location estimates look cleaner than
they really are.

## Correlation with wardrive data

The existing WiFi/BLE wardrive tables are useful context for LoRa
sightings. They can answer "what RF environment was nearby when this
packet was heard?" and "have we seen this place before?"

Useful joins:

- LoRa sighting near WiFi/BLE fingerprint cluster
- strongest LoRa RSSI/SNR near a known GPS track segment
- repeated node sightings across multiple passes through the same area
- LoRa activity by time window, route, channel preset, or neighborhood
- decoded Meshtastic position versus receiver-derived confidence zone
- relay activity versus origin node activity

The first product should be a sighting map and heatmap, not an exact
locator. Exact-looking pins should be reserved for decoded/self-reported
node positions or high-confidence estimates with enough samples.

## Location estimates

Start with simple, explainable outputs:

- per-node sighting dots colored by RSSI and SNR
- per-node "best heard here" marker
- coverage footprint polygon from repeated sightings
- confidence zone based on strongest samples and route geometry
- timeline playback of node activity along the drive route

RSSI-only ranging is noisy in a city. Buildings, vehicle body shielding,
antenna orientation, multipath, hills, and node antenna quality will
all distort the estimate. Multiple receivers, directional antennas, and
repeated passes from different streets are the path to better location
confidence.

## Optional sparse beacon mode

Occasional broadcasts can improve discovery, but they are active
participation in the mesh rather than pure passive wardriving. This
mode should be optional, plainly labeled, and designed to be a polite
presence beacon instead of an interrogation tool.

Recommended constraints:

- default off
- public/default channel only unless the operator owns the channel
- normal Meshtastic NodeInfo or presence-style packet
- no ACK request
- low hop limit, preferably 0 or 1
- long randomized interval, such as 10 to 30 minutes
- global transmit budget with a hard packets-per-hour cap
- per-node cooldown if targeted requests are ever added
- channel-utilization guard; suppress transmission when the mesh is busy
- never probe encrypted/private channels that the operator is not part of

The database must label these observations distinctly:

- `passive`: heard without Pisces Moon transmitting
- `self_initiated`: emitted by Pisces Moon
- `response_to_self`: likely response to a Pisces Moon packet
- `relay_of_self`: rebroadcast or route effect caused by Pisces Moon

Maps should be able to hide self-induced observations so the passive
dataset remains clean.

## Implementation sketch

Add a LoRa/Meshtastic observation path beside the current WiFi, BLE,
probe, packet, and GPS tables. The event schema should be append-only
at first; later analysis can build per-node rollups from raw sightings.

Proposed raw table shape:

```sql
CREATE TABLE lora_seen (
  id                  INTEGER PRIMARY KEY AUTOINCREMENT,
  ts_ms               INTEGER,
  rx_lat              REAL,
  rx_lng              REAL,
  rx_alt_m            REAL,
  gps_sats            INTEGER,
  gps_age_ms          INTEGER,
  freq_hz             INTEGER,
  channel_name        TEXT,
  sf                  INTEGER,
  bw_hz               INTEGER,
  cr                  TEXT,
  sync_word           INTEGER,
  rssi                INTEGER,
  snr                 REAL,
  packet_len          INTEGER,
  origin_node         TEXT,
  tx_node             TEXT,
  dst_node            TEXT,
  hop_limit           INTEGER,
  hop_start           INTEGER,
  next_hop            TEXT,
  relay_node          TEXT,
  payload_type        TEXT,
  decrypt_state       TEXT,
  observation_source  TEXT,
  raw_header_hex      TEXT
);
```

Later rollup tables can summarize:

- node first_seen and last_seen
- best RSSI/SNR sighting
- packet count by day/channel/source
- decoded self-reported position history
- estimated location center and confidence radius
- relay/origin split counts

## UI direction

Fold this into the wardrive map rather than creating a separate
first-class map surface immediately. The operator wants one moving
picture of the route:

- WiFi/BLE/probe layers remain as they are
- LoRa/Meshtastic sightings become a toggleable layer
- node detail shows all sightings, best-heard point, decoded position
  if present, confidence notes, and whether the view is passive-only
- sparse beacon mode is visibly distinct from passive mode and cannot
  be enabled accidentally

The important promise: Pisces Moon should be honest about confidence.
"Heard here" is always safe to show. "Probably near here" needs enough
samples, and "is here" should only come from decoded/self-reported GPS
or an operator-controlled test node.
