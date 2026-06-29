# Planetopia Architecture Review & Change Spec

**Date:** 2026-06-29
**Scope:** Full-stack review — firmware (Planetopia-nodes) + server (motionSensorServer)
**Context:** Interactive art installations, 20–50 ESP32 nodes, multi-hop ESP-NOW mesh, bidirectional sensor-to-effect reactions, 50–200ms latency budget, USB-serial gateway to Go orchestrator, artist-facing public API

---

## 1. Problem Statement

Planetopia is a platform for interactive art installations. Artists deploy 20–50 ESP32 nodes in a venue. PIR sensors detect motion; the server reacts by sending commands to other nodes (LEDs, sound triggers, custom outputs). Artists write the reaction logic against a public API — the platform provides reliable event delivery and command dispatch.

The current architecture has a correct security model, solid serialization (nanopb/protobuf), and good layering. However, two critical routing bugs mean the mesh silently fails for any node more than one hop from the master, making multi-room installations non-functional.

---

## 2. Current Architecture (as-built)

```
[Artist Code]
     ↕ REST API (polling only)
[orchestrator — Go]
     ↕ Kafka (event log)
     ↕ USB serial / nanopb protobuf frames
[ESP32 master node — Serial_Adapter]
     ↕ ESP-NOW (AES encrypted, ECDH per-peer LMK post-enrollment)
[ESP32 sensor nodes — PIR_Adapter, etc.]
```

### What works well
- ESP-NOW with ECDH enrollment + per-peer LMK: solid security model for embedded hardware
- Replay protection (boot epoch + seq ring buffer): covers reboot attacks
- TOFU master MAC enforcement: anti-impersonation
- Deferred EEPROM writes (dirty-flag pattern): flash wear protection
- Relay jitter on beacons (10–73ms random): eliminates collision bursts
- nanopb over length-prefixed serial frames: compact, correct framing
- Docker-composed server with Kafka for event sourcing: good foundation for artist API
- Adapter factory pattern: clean hardware abstraction

---

## 3. Critical Bugs

### 3.1 Multi-hop sensor data relay is missing (firmware)

**File:** `main/src/Mesh/Mesh.cpp:924` — `processAdapterData()`

When an intermediate node receives `MESH_TYPE_ADAPTER_DATA` not targeted at itself, it calls `externalRecvCallback(msg)` → `adapter->onMeshData(msg)` → `PIR_Adapter::onMeshDataImpl()` → **no-op**. The message is dropped.

The routing table is correct (beacon propagation builds `currentMaster.nextHop` for every node), but the relay mechanism for actual sensor data does not exist. Every sensor node beyond direct ESP-NOW range of the master is silently offline.

**Impact:** Multi-room art installations with 20–50 nodes are non-functional without this fix.

### 3.2 Multi-hop enrollment (JOIN_ACK) cannot propagate (firmware)

**File:** `main/src/Mesh/Mesh.cpp:1040` — `processJoinAck()`

`processJoinAck()` only processes the ACK if it's addressed to the current node. There is no relay for nodes beyond one hop. The master broadcasts the JOIN_ACK but it never reaches a node that is 2+ hops away.

**Impact:** Nodes more than one hop from the master can never enroll.

### 3.3 TX power set sends raw bytes, bypasses protobuf (server)

**File:** `server/orchestrator/mesh/server.go:584` — `SetTxPowerPreset()`

The method builds a raw `[0xA1, preset]` byte array with a 2-byte length header and calls `WriteRaw()`. The firmware's `handleCompleteFrame()` tries to nanopb-decode every frame. Raw bytes are not valid protobuf — decoding fails and the function returns. TX power set from the server does nothing.

---

## 4. High-Priority Missing Features

### 4.1 PIR nodes invisible to server (both sides)

Health reports (`0xB1`) are only sent by `Serial_Adapter` (the master). PIR nodes never appear in `NodeRegistry`. For 20–50 node art installations, the server has no visibility into whether sensor nodes are alive.

### 4.2 No real-time event push to artists (server)

Dashboard and external code poll REST endpoints. PIR events are written to Kafka but there is no Server-Sent Events or WebSocket endpoint. The art reaction loop (PIR fires → artist code reacts → sends command to node B) cannot meet a 50–200ms latency budget via polling.

### 4.3 No targeted node command API (server)

`BuildAdapterDataMessage()` exists but there is no HTTP endpoint for it. Artists cannot send custom payloads to a specific node. The only send paths are broadcast and config-set.

### 4.4 No reaction loop infrastructure (server)

Kafka has a `motion-trigger` topic but nothing consumes it. The path from "PIR event received" to "command sent to another node" is entirely unimplemented.

---

## 5. Medium-Priority Issues

### 5.1 Single point of failure — master node (firmware + server)

One master node, one USB cable. If either fails, all sensor data stops reaching the server. For "reliability is everything" installations this is unacceptable.

### 5.2 12-byte data field too small (both sides)

`mesh_message.data[12]` is already worked around in enrollment (3-chunk 11-byte messages for a 32-byte key). For art installations with rich payloads (LED animation parameters, audio triggers, custom sensor data), 12 bytes will be a constant constraint.

`server/orchestrator/mesh/constants.go:48` — `MaxDataLength = 12` must stay in sync.

### 5.3 WDT reboot loop wipes EEPROM (firmware)

**File:** `main/main.ino:107`

Five consecutive WDT resets trigger `em.clearAll()` followed by an infinite halt. In a running installation this destroys all enrollment data, peer lists, and configuration. Recovery requires re-enrolling all nodes.

### 5.4 Circular dependency: Mesh ↔ Serial_Adapter (firmware)

`Mesh.cpp` includes `Serial_Adapter.h` to call the static `relayEnrollmentToServer()`. `Serial_Adapter.cpp` includes `Mesh.h`. This is a real circular include at the `.cpp` level. The current build works but the coupling makes both modules harder to test and evolve independently.

### 5.5 Online timeout equals health report interval (server)

**File:** `server/orchestrator/mesh/api.go:240`

`GetOnlineNodes(30 * time.Second)` uses the same 30s as the health report interval. A single missed report makes a node appear offline. Should be 2–3× the report interval.

### 5.6 JOIN_ACK MAC field inconsistency (server)

**File:** `server/orchestrator/mesh/server.go:435`

`ApproveEnrollment()` sets `OriginMacAddress: node.MAC[:]`. The firmware's `Serial_Adapter::handleCompleteFrame()` reads `msg.originMacAddress` to identify which node to enroll — this works by accident but contradicts the `server_requirements.md` spec which says to use `TargetMacAddress`.

---

## 6. Proposed Changes

### 6.1 Multi-hop uplink relay (firmware — critical)

Add relay logic to `Mesh::processAdapterData()`:

```cpp
void Mesh::processAdapterData(const mesh_message& msg) {
  // If we're not the destination, relay toward master
  if (!isMaster &&
      memcmp(msg.targetMacAddress, deviceMacAddress, 6) != 0 &&
      msg.hopCount < planetopia::config::MAX_HOPS) {
    if (isReplay(msg)) return;  // dedup: replay cache prevents loops
    mesh_message relay = msg;
    relay.hopCount++;
    memcpy(relay.lastHopMacAddress, deviceMacAddress, 6);
    transmitCore(relay.dataType, relay.data, MESH_TYPE_ADAPTER_DATA, &relay);
    return;
  }
  // Config-modifying opcode guard (existing)
  ...
  if (externalRecvCallback) externalRecvCallback(msg);
}
```

The existing replay cache (16-entry ring buffer keyed on `mac+epoch+seq`) provides dedup so each message propagates exactly once per path.

### 6.2 Multi-hop downlink relay (firmware — critical)

Non-master nodes that receive `MESH_TYPE_SERIAL_CMD_BROADCAST` or targeted `MESH_TYPE_ADAPTER_DATA` not addressed to them should relay outward (away from master, toward all peers) using the same broadcast-with-dedup pattern as the beacon relay.

Add to `processAdapterData()` handling and add a new `relayDownlink()` helper:

```cpp
void Mesh::relayDownlink(const mesh_message& msg) {
  if (isReplay(msg)) return;
  if (msg.hopCount >= planetopia::config::MAX_HOPS) return;
  mesh_message relay = msg;
  relay.hopCount++;
  memcpy(relay.lastHopMacAddress, deviceMacAddress, 6);
  broadcastToAllPeers(relay);
}
```

Called from:
- `processAdapterData()` when `messageType == MESH_TYPE_SERIAL_CMD_BROADCAST`
- `processAdapterData()` when `messageType == MESH_TYPE_ADAPTER_DATA` and target is not self and not master

### 6.3 Multi-hop JOIN_ACK relay (firmware — critical)

In `processJoinAck()`, before the `targetMacAddress` check, add:

```cpp
// Relay if not addressed to us
if (memcmp(msg.targetMacAddress, deviceMacAddress, 6) != 0 &&
    !isReplay(msg) &&
    msg.hopCount < planetopia::config::MAX_HOPS) {
  relayDownlink(msg);  // reuse the downlink relay helper
  return;
}
```

### 6.4 PIR health heartbeat (firmware + server)

Add a new serial opcode `OP_NODE_HEALTH = 0xB2` for non-serial nodes to report health through the mesh.

**Firmware:** Every non-master node sends a compact health packet through the mesh every 30s via `MESH_TYPE_ADAPTER_DATA` with `dataType=SERIAL_ADAPTER` and `data[0]=0xB2`:

```
data[0]  = 0xB2          (OP_NODE_HEALTH)
data[1]  = adapterType   (int8)
data[2..7] = own MAC     (6 bytes)
data[8..11] = uptime seconds (uint32 LE)
```

This is 12 bytes total — fits current data field. `hopCount` in the message header gives the server the hop distance.

**Server:** Add `OpNodeHealth byte = 0xB2` to constants. In `handleSerialData()`, add a case for `0xB2` that calls `UpdateNode()` with the parsed MAC, adapter type, uptime, and hop count from `msg.HopCount`.

### 6.5 Fix TX power protobuf wrapping (server — bug fix)

Replace `WriteRaw()` in `SetTxPowerPreset()` with a proper protobuf frame:

```go
func (ms *MeshServer) SetTxPowerPreset(preset uint8) error {
    payload := make([]byte, MaxDataLength)
    payload[0] = OpTxPowerSet
    payload[1] = preset
    msg := &MeshMessage{
        MessageType: MessageTypeAdapterData,
        DataType:    AdapterTypeSerial,
        Data:        payload,
    }
    return ms.serialComm.WriteFrame(msg)
}
```

### 6.6 Artist event streaming API (server)

Add a Server-Sent Events endpoint:

```
GET /events
Authorization: Bearer <api-key>
Accept: text/event-stream
```

The server maintains a set of SSE clients. When the message processor handles a `pir_motion`, `enrollment_request`, `node_health`, or `node_joined` event, it fans out to all connected SSE clients.

Event format:
```
event: pir_motion
data: {"mac":"aa:bb:cc:dd:ee:ff","hopCount":2,"timestamp":1751234567}

event: node_health
data: {"mac":"aa:bb:cc:dd:ee:ff","adapterType":0,"uptime":3600,"hopCount":1,"online":true}

event: enrollment_request
data: {"mac":"aa:bb:cc:dd:ee:ff","publicKey":"3a7f2b..."}
```

Artists connect with `EventSource` (browser) or `curl -N` / `http.Get` streaming (any language).

### 6.7 Targeted node command API (server)

Add HTTP endpoint:

```
POST /nodes/{mac}/send
Content-Type: application/json
Authorization: Bearer <api-key>

{
  "dataType": 2,
  "data": [1, 255, 0, 128]
}
```

Builds `MeshMessage{type=ADAPTER_DATA, target=mac, dataType, data}` and calls `SendMessage()`. Artists use this to trigger effects on specific nodes.

Validation: `len(data) <= MaxDataLength`. Return 400 if node MAC not in registry (optional — can relax for provisioning scenarios).

### 6.8 Increase data field (both sides)

**Firmware:** Change `mesh_message.data[12]` → `data[32]`. Update `static_assert(sizeof(mesh_message) == 75)` to new size (93 bytes: +20). Update `mesh.proto` `data` field description. Regenerate nanopb files with updated `.options` for max size.

**Server:** Change `MaxDataLength = 12` → `MaxDataLength = 32` in `constants.go`. Update `ParseHealthReport()` bounds check accordingly.

This is a **breaking wire protocol change**. All nodes and the server must be updated atomically. Increment `PROTO_VERSION` to `2` in firmware and add version guard in server's `handleMessage()`.

### 6.9 Backup master (firmware + server — design only)

**Option A (firmware-only):** When `checkMasterTimeout()` detects stale master, a pre-designated backup node (flag in EEPROM) promotes itself. Without a second serial connection, the server won't receive data from the backup — this is only useful for keeping the mesh alive while the operator replaces the master hardware.

**Option B (dual serial + server):** Server opens two serial ports. Master A and master B both have `Serial_Adapter`. Server detects which is active via health reports and ignores the inactive one. When master A goes stale, master B takes over. This requires two USB connections to the server host.

**Option C (WiFi fallback):** Master node joins a local WiFi AP and sends events over HTTP/WebSocket as a fallback when serial is unavailable. Requires configuring WiFi credentials on the master (and disabling during normal operation to avoid AP/ESP-NOW channel conflicts).

**Recommendation:** Option A for now — add `BACKUP_MASTER_MAC` to EEPROM schema. A second node is configured as backup; if it detects master stale for `2× STALE_MASTER_THRESHOLD_MS`, it sets `isMaster=true`. Full failover with server awareness (Option B or C) is a separate spec.

### 6.10 Remove EEPROM wipe on WDT loop (firmware)

**File:** `main/main.ino:105-108`

Replace:
```cpp
em.clearAll();
while (true) { delay(1000); }
```

With:
```cpp
Serial.println("[BOOT] WDT loop detected — halting. Manual reset required.");
while (true) { delay(1000); }
```

If a hardware watchdog reset loop is caused by bad state, manual operator intervention is safer than silent data destruction. Add a separate "factory reset" path accessible only via the physical reset button sequence (already implemented).

### 6.11 Decouple Mesh from Serial_Adapter (firmware — refactor)

Replace the static call `Serial_Adapter::relayEnrollmentToServer()` in `Mesh.cpp` with a registered callback:

```cpp
// In Mesh.h
typedef void (*EnrollmentRelayFn)(const uint8_t mac[6], const uint8_t pubKey[32]);
void setEnrollmentRelayFn(EnrollmentRelayFn fn);

// In main.ino (during setup)
mesh.setEnrollmentRelayFn(Serial_Adapter::relayEnrollmentToServer);
```

Removes the `#include "Serial_Adapter/Serial_Adapter.h"` from `Mesh.cpp`.

### 6.12 Fix JOIN_ACK field (server)

**File:** `server/orchestrator/mesh/server.go:435`

Change:
```go
ackMsg := &MeshMessage{
    MessageType:      MessageTypeJoinAck,
    OriginMacAddress: node.MAC[:],
    PublicKey:        node.PublicKey[:],
}
```

To:
```go
ackMsg := &MeshMessage{
    MessageType:      MessageTypeJoinAck,
    TargetMacAddress: node.MAC[:],
    PublicKey:        node.PublicKey[:],
}
```

**Also update firmware:** `Serial_Adapter::handleCompleteFrame()` for JOIN_ACK: read `msg.targetMacAddress` instead of `msg.originMacAddress` when calling `enrollPeer()`.

### 6.13 Fix online timeout (server)

**File:** `server/orchestrator/mesh/api.go:240`

Change:
```go
onlineNodes := registry.GetOnlineNodes(30 * time.Second)
```
To:
```go
onlineNodes := registry.GetOnlineNodes(75 * time.Second) // 2.5× health interval
```

---

## 7. Artist API Design

The server exposes a platform for artists to build reaction logic. Artists are not expected to understand ESP-NOW, protobuf, or mesh routing.

### Minimal artist interface

**Subscribe to events:**
```
GET /events  (SSE stream)
```

**Send to a specific node:**
```
POST /nodes/{mac}/send
{"dataType": 0, "data": [1]}
```

**Broadcast to all nodes:**
```
POST /broadcast
{"dataType": 0, "data": [1]}
```

**List nodes (with online status):**
```
GET /nodes
```

### Artist workflow

1. Open the dashboard, enroll nodes (approve via enrollment UI)
2. Note each node's MAC address and physical location
3. Write a script that connects to `/events` SSE stream
4. On `pir_motion` event from a known MAC, call `POST /nodes/{target_mac}/send` with a payload that the target node's firmware interprets
5. Deploy script alongside the installation

### Artist payload format (node firmware responsibility)

Each adapter type defines its own payload schema within the 32-byte data field. The platform is payload-agnostic. Documenting per-adapter payload schemas is the firmware team's responsibility (see `docs/adapter_development_guide.md`).

---

## 8. Implementation Order

| Phase | Changes | Risk |
|---|---|---|
| 1 — Fix routing | 6.1, 6.2, 6.3 (multi-hop relay) | High — requires careful testing of dedup |
| 2 — Fix bugs | 6.5 (TX power), 6.10 (WDT), 6.13 (timeout) | Low |
| 3 — Visibility | 6.4 (PIR health heartbeat) | Medium |
| 4 — Artist API | 6.6 (SSE), 6.7 (targeted send) | Medium |
| 5 — Data field | 6.8 (32-byte data, proto v2) | High — breaking change, coordinate |
| 6 — Reliability | 6.9 (backup master) | High — design carefully |
| 7 — Cleanup | 6.11 (decouple Mesh/Serial_Adapter), 6.12 (JOIN_ACK field) | Low |

---

## 9. Open Questions

1. **Backup master strategy:** Is Option A (firmware-only backup promotion, no server awareness) sufficient for the first deployment, or does Option B (dual serial) need to be in Phase 1?

2. **Data field size increase:** Proto version bump means a coordinated firmware + server deploy. Is that acceptable, or should we keep 12 bytes and use chunking for large payloads?

3. **Artist payload protocol:** Should there be a standard Planetopia payload envelope (e.g., first byte = effect type, remaining = parameters) that all nodes understand? Or is it fully per-adapter and per-installation?

4. **WiFi fallback on master:** ESP32 can run ESP-NOW and WiFi-STA simultaneously on the same channel. Is the reliability gain worth the added complexity for Option C?
