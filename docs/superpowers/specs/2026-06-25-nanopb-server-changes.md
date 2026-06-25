# Server-Side Changes Required: nanopb Serial Protobuf Migration

**Status:** Required — firmware is deployed; server must be updated to enable enrollment.
**Scope:** `motionSensorServer/server/orchistrator/mesh/` only.
**Do not implement from Planetopia-nodes repo.** This spec documents what must change in `motionSensorServer`.

---

## Context

The firmware has been migrated to nanopb Protobuf encoding on the serial link. The server's `proto.Unmarshal` / `proto.Marshal` already handles the wire format correctly — the existing `ReadFrame`/`WriteFrame` in `serial.go` are unchanged.

However, the firmware's proto schema now has 11 fields (the server's has 7), and two new message types are required for the enrollment handshake:

- `MessageTypeEnrollment = 2` — firmware → server: "I want to join"
- `MessageTypeJoinAck = 4` — server → firmware: "approved" or "rejected"

The server's `MessageTypeSerialCmdBroadcast = 3` is **unchanged** and must remain 3.

**Existing flows that continue to work without server changes:**
- `ADAPTER_DATA (0)` — proto3 unknown fields (8-11) are silently ignored by the server
- `MASTER_BEACON (1)` — same, safe to deploy firmware first

**Flow that requires server changes:**
- Enrollment handshake — firmware sends `messageType=2` with `public_key`; server must respond with `messageType=4`

---

## Wire Protocol Expectations

### Enrollment request (firmware → server)

```
messageType  = 2
public_key   = 32 bytes (Curve25519 public key of the enrolling node)
originMacAddress = 6 bytes (MAC of the enrolling node)
protoVersion = 1
```
All other fields are zero/empty.

### JOIN_ACK — approval (server → firmware)

```
messageType     = 4
originMacAddress = 6 bytes (MAC of the node being approved — echo back)
public_key      = 32 bytes (the node's Curve25519 public key — echo back for confirmation)
protoVersion    = 1
```

### JOIN_ACK — rejection (server → firmware)

```
messageType     = 4
originMacAddress = 6 bytes (MAC of the node being rejected — echo back)
public_key      = absent / empty (no has_public_key)
protoVersion    = 1
```

On the firmware side, `handleCompleteFrame()` checks whether `enrollmentPublicKey` is non-zero after decoding; if absent (zero), it treats the response as a rejection.

---

## Required Changes

### 1. `mesh.proto` — add fields 8–11

Add to `MeshMessage`:

```proto
  // Replay-protection fields (set by firmware; server passes through)
  uint32 epochNum    = 8;  // boot epoch of origin node
  uint32 seqNum      = 9;  // per-boot sequence counter
  uint32 protoVersion = 10; // wire protocol version (currently 1)

  // Curve25519 public key — present ONLY on ENROLLMENT and JOIN_ACK messages
  optional bytes public_key = 11; // max 32 bytes
```

The `optional` keyword is required for field 11 — it ensures proto3 generates `has_public_key` semantics so the field is omitted on non-enrollment messages. Fields 8-10 are plain `uint32` (always zero on non-enrollment messages; server can ignore or forward them).

**Regenerate after updating:**
```bash
cd motionSensorServer
protoc --go_out=. --go_opt=paths=source_relative \
  server/orchistrator/mesh/mesh.proto
```
This updates `mesh.pb.go`.

---

### 2. `constants.go` — add enrollment message type constants

Add to the `Message Types` const block:

```go
MessageTypeEnrollment uint32 = 2 // Firmware→server: node requesting to join mesh
MessageTypeJoinAck    uint32 = 4 // Server→firmware: enrollment approved or rejected
```

The full block after change:
```go
const (
    MessageTypeAdapterData        uint32 = 0
    MessageTypeMasterBeacon       uint32 = 1
    MessageTypeEnrollment         uint32 = 2 // new
    MessageTypeSerialCmdBroadcast uint32 = 3 // unchanged
    MessageTypeJoinAck            uint32 = 4 // new
)
```

---

### 3. `server.go` — add handleEnrollment(), wire into handleMessage()

**Step 1: Update handleMessage() to dispatch on type 2:**

```go
func (ms *MeshServer) handleMessage(msg *MeshMessage) error {
    // ... existing logging ...
    switch msg.MessageType {
    case MessageTypeAdapterData:
        return ms.handleAdapterData(msg)
    case MessageTypeMasterBeacon:
        return ms.handleMasterBeacon(msg)
    case MessageTypeEnrollment:          // NEW
        return ms.handleEnrollment(msg)  // NEW
    default:
        log.Printf("Unknown message type: %d", msg.MessageType)
        return nil
    }
}
```

**Step 2: Add handleEnrollment():**

```go
// handleEnrollment processes an enrollment request from a new node.
// It validates the public key, registers the node as pending, and
// sends a JOIN_ACK. For now, auto-approves all requests; replace with
// admin-approval flow when nodeauth persistence is integrated.
func (ms *MeshServer) handleEnrollment(msg *MeshMessage) error {
    mac := msg.OriginMacAddress
    pubKey := msg.PublicKey

    if len(mac) != 6 {
        return fmt.Errorf("enrollment: invalid MAC length %d", len(mac))
    }
    if len(pubKey) != 32 {
        return fmt.Errorf("enrollment: invalid public key length %d", len(pubKey))
    }

    log.Printf("[ENROLLMENT] Node %x requesting enrollment, pubKey=%x", mac, pubKey)

    // TODO: replace with nodeauth.Registry.AddPending() + admin approval flow
    // For now: auto-approve all enrollment requests.
    approved := true

    ack := &MeshMessage{
        MessageType:     MessageTypeJoinAck,
        OriginMacAddress: mac,
        ProtoVersion:    1,
    }
    if approved {
        ack.PublicKey = pubKey // echo back = approval
        log.Printf("[ENROLLMENT] Auto-approving node %x", mac)
    } else {
        // No public_key = rejection signal to firmware
        log.Printf("[ENROLLMENT] Rejecting node %x", mac)
    }

    if err := ms.serialComm.WriteFrame(ack); err != nil {
        return fmt.Errorf("enrollment: failed to send JOIN_ACK: %w", err)
    }
    return nil
}
```

**Note on `ProtoVersion`:** After regenerating `mesh.pb.go`, `MeshMessage` will have a `ProtoVersion uint32` field (field 10). Set it to 1 on server-originated JOIN_ACK messages.

---

### 4. `mesh.pb.go` — regenerate (do not edit by hand)

After updating `mesh.proto` and running `protoc`, the regenerated `mesh.pb.go` will:
- Add `EpochNum`, `SeqNum`, `ProtoVersion uint32` fields to `MeshMessage`
- Add `PublicKey []byte` field (proto3 `optional bytes` generates `*[]byte` or `[]byte` depending on protoc-gen-go version — verify the generated accessor for `GetPublicKey()` and use that in `handleEnrollment`)

Verify the generated accessor:
```bash
grep -E "PublicKey|GetPublicKey" server/orchistrator/mesh/mesh.pb.go
```
If the field generates as a pointer (`*[]byte`), update `handleEnrollment` accordingly:
```go
// If PublicKey is *[]byte:
if approved {
    pk := make([]byte, 32)
    copy(pk, pubKey)
    ack.PublicKey = pk
}
```

---

## Out of Scope (Future Work)

- **nodeauth package integration** — `motionSensorServer/.worktrees/full-review/server/orchistrator/nodeauth/` has a `Registry` with `AddPending()` / `Approve()` / `Reject()` methods. Integrating this would replace the auto-approve TODO in `handleEnrollment()` with a proper admin-controlled flow. This is a separate feature.
- **Replay protection** — `epochNum` and `seqNum` fields (8-9) are in the proto but the server does not need to validate them for enrollment. They are included for future server-side replay detection.
- **LMK derivation** — The firmware derives a per-link session key using Curve25519 ECDH after enrollment. The server does not participate in key exchange; the public key is only used to identify the node.

---

## Testing

After implementing:

1. Build server: `cd motionSensorServer && go build ./...`
2. Run server tests: `go test ./server/orchistrator/mesh/...`
3. End-to-end: Flash updated firmware, observe serial log — firmware sends enrollment request on boot; server should log `[ENROLLMENT] Auto-approving node XX:XX:XX:XX:XX:XX` and reply with JOIN_ACK.
