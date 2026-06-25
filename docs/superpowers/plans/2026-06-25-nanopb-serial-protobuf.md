# nanopb Serial Protobuf Migration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace hand-rolled Protobuf encode/decode in Serial_Adapter with nanopb, fix the enrollment relay to send valid Protobuf frames (currently silently broken), and align the serial wire protocol with the server's `proto.Unmarshal` expectations.

**Architecture:** Four tasks in dependency order: (1) add nanopb runtime + proto schema files to the repo, (2) extend `mesh_message` struct and `MeshMessageType` enum, (3) replace Serial_Adapter codec with nanopb calls, (4) update the host test build and delete dead ProtobufCodec code. The server-side changes are specced separately and not implemented here.

**Tech Stack:** nanopb 0.4.9.1 (C runtime already downloaded via pip package), ESP32 Arduino, GoogleTest for host tests.

## Global Constraints

- nanopb version: **0.4.9.1** — runtime files sourced from GitHub tarball (not the pip package, which ships generator only)
- nanopb generator: `/Users/benjamin.swanepoel/Library/Python/3.9/lib/python/site-packages/nanopb/generator/nanopb_generator.py`
- Proto schema lives at `main/proto/mesh.proto` — generated files (`mesh.pb.h`, `mesh.pb.c`) are committed to the repo
- `mesh_message` static_assert changes from `== 43` to `== 75` (adding `enrollmentPublicKey[32]`)
- `MESH_TYPE_JOIN_ACK = 4` (was 3) — 3 is now `MESH_TYPE_SERIAL_CMD_BROADCAST` (server→device only, matches server constant)
- `has_public_key` pattern for `public_key` field (no `fixed_length:true`) — avoids 34 wasted bytes per non-enrollment frame
- `data_count` set to actual payload length (currently always 12; keep 12 but use `sizeof(msg.data)`)
- All outgoing serial frames must set `protoVersion = 1`
- Arduino compile: `arduino-cli compile --fqbn esp32:esp32:esp32da main` — must succeed after every task
- Host test build: `cd tests && cmake -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build && cd build && ctest --output-on-failure`
- No new heap allocations; nanopb encodes/decodes into caller-provided stack buffers
- Branch: `feat/nanopb-serial-protobuf`

---

### Task 1: nanopb runtime + proto schema + generated files

**Files:**
- Create: `main/proto/mesh.proto`
- Create: `main/proto/mesh.options`
- Create: `main/src/Mesh/serialization/nanopb/pb.h`
- Create: `main/src/Mesh/serialization/nanopb/pb_encode.h`
- Create: `main/src/Mesh/serialization/nanopb/pb_encode.c`
- Create: `main/src/Mesh/serialization/nanopb/pb_decode.h`
- Create: `main/src/Mesh/serialization/nanopb/pb_decode.c`
- Create: `main/src/Mesh/serialization/nanopb/pb_common.h`
- Create: `main/src/Mesh/serialization/nanopb/pb_common.c`
- Create: `main/src/Mesh/serialization/mesh.pb.h` (generated)
- Create: `main/src/Mesh/serialization/mesh.pb.c` (generated)

**Interfaces:**
- Produces: `MeshMessage`, `MeshMessage_fields`, `MeshMessage_init_zero`, `pb_ostream_from_buffer`, `pb_istream_from_buffer`, `pb_encode`, `pb_decode`, `PB_GET_ERROR` — used by Task 3

- [ ] **Step 1: Download nanopb 0.4.9.1 runtime C files**

```bash
cd /tmp
curl -L https://github.com/nanopb/nanopb/archive/refs/tags/0.4.9.1.tar.gz -o nanopb-0.4.9.1.tar.gz
tar xzf nanopb-0.4.9.1.tar.gz
cp nanopb-0.4.9.1/pb.h         /Users/benjamin.swanepoel/projects/personal/Planetopia-nodes/main/src/Mesh/serialization/nanopb/
cp nanopb-0.4.9.1/pb_encode.h  /Users/benjamin.swanepoel/projects/personal/Planetopia-nodes/main/src/Mesh/serialization/nanopb/
cp nanopb-0.4.9.1/pb_encode.c  /Users/benjamin.swanepoel/projects/personal/Planetopia-nodes/main/src/Mesh/serialization/nanopb/
cp nanopb-0.4.9.1/pb_decode.h  /Users/benjamin.swanepoel/projects/personal/Planetopia-nodes/main/src/Mesh/serialization/nanopb/
cp nanopb-0.4.9.1/pb_decode.c  /Users/benjamin.swanepoel/projects/personal/Planetopia-nodes/main/src/Mesh/serialization/nanopb/
cp nanopb-0.4.9.1/pb_common.h  /Users/benjamin.swanepoel/projects/personal/Planetopia-nodes/main/src/Mesh/serialization/nanopb/
cp nanopb-0.4.9.1/pb_common.c  /Users/benjamin.swanepoel/projects/personal/Planetopia-nodes/main/src/Mesh/serialization/nanopb/
```

Verify 7 files present:
```bash
ls /Users/benjamin.swanepoel/projects/personal/Planetopia-nodes/main/src/Mesh/serialization/nanopb/
```
Expected: `pb.h pb_encode.h pb_encode.c pb_decode.h pb_decode.c pb_common.h pb_common.c`

- [ ] **Step 2: Create proto schema**

Create `main/proto/mesh.proto`:
```proto
syntax = "proto3";
package mesh;

message MeshMessage {
  uint32 messageType       = 1;
  sint32 dataType          = 2;
  bytes  originMacAddress  = 3;
  bytes  targetMacAddress  = 4;
  bytes  lastHopMacAddress = 5;
  bytes  data              = 6;
  uint32 hopCount          = 7;
  uint32 epochNum          = 8;
  uint32 seqNum            = 9;
  uint32 protoVersion      = 10;
  bytes  public_key        = 11;
}
```

Create `main/proto/mesh.options`:
```
MeshMessage.originMacAddress  max_size:6  fixed_length:true
MeshMessage.targetMacAddress  max_size:6  fixed_length:true
MeshMessage.lastHopMacAddress max_size:6  fixed_length:true
MeshMessage.data              max_size:12
MeshMessage.public_key        max_size:32
```

- [ ] **Step 3: Generate nanopb C files**

```bash
cd /Users/benjamin.swanepoel/projects/personal/Planetopia-nodes
python3 /Users/benjamin.swanepoel/Library/Python/3.9/lib/python/site-packages/nanopb/generator/nanopb_generator.py \
  --proto-path=main/proto \
  --output-dir=main/src/Mesh/serialization \
  main/proto/mesh.proto
```

Expected output: two new files created:
- `main/src/Mesh/serialization/mesh.pb.h`
- `main/src/Mesh/serialization/mesh.pb.c`

Verify the generated header contains the right structure:
```bash
grep -E "has_public_key|public_key\[|data_count|MeshMessage_fields" \
  main/src/Mesh/serialization/mesh.pb.h
```
Expected: `has_public_key` bool present (no `fixed_length` on public_key → optional field), `data_count` present (data field has max_size without fixed_length).

- [ ] **Step 4: Verify the repo compiles with nanopb present (no integration yet)**

The nanopb files are not yet included anywhere, so the Arduino compile must still pass unchanged:
```bash
arduino-cli compile --fqbn esp32:esp32:esp32da main
```
Expected: success.

- [ ] **Step 5: Commit**

```bash
git add main/proto/mesh.proto \
        main/proto/mesh.options \
        main/src/Mesh/serialization/nanopb/ \
        main/src/Mesh/serialization/mesh.pb.h \
        main/src/Mesh/serialization/mesh.pb.c
git commit -m "feat: add nanopb 0.4.9.1 runtime + proto schema + generated mesh.pb files"
```

---

### Task 2: mesh_message struct + MeshMessageType enum

**Files:**
- Modify: `main/src/Mesh/Mesh.h` — `mesh_message` struct, `MeshMessageType` enum, `static_assert`
- Modify: `main/src/Mesh/Mesh.cpp` — fix any MESH_TYPE_JOIN_ACK == 3 → 4 references

**Interfaces:**
- Consumes: nothing from Task 1 (independent struct change)
- Produces: `mesh_message` with `enrollmentPublicKey[32]` at offset 43, sizeof=75; `MESH_TYPE_SERIAL_CMD_BROADCAST=3`, `MESH_TYPE_JOIN_ACK=4`

- [ ] **Step 1: Update MeshMessageType enum in Mesh.h**

In `main/src/Mesh/Mesh.h`, replace the enum block (currently at line 29):

```cpp
// --- Mesh protocol message type ---
enum MeshMessageType : uint8_t {
  MESH_TYPE_ADAPTER_DATA         = 0,
  MESH_TYPE_MASTER_BEACON        = 1,
  MESH_TYPE_ENROLLMENT           = 2,
  MESH_TYPE_SERIAL_CMD_BROADCAST = 3,  // server→device only
  MESH_TYPE_JOIN_ACK             = 4,  // server→device only; was 3, changed to avoid collision with SERIAL_CMD_BROADCAST
};
```

- [ ] **Step 2: Add enrollmentPublicKey to mesh_message struct in Mesh.h**

Replace the struct and static_assert (currently lines 39-53):

```cpp
// --- Mesh message struct (packed: wire protocol, no padding) ---
struct __attribute__((packed)) mesh_message {
  uint8_t protoVersion;           // Always PROTO_VERSION (1)
  MeshMessageType messageType;
  adapter_types dataType;
  uint8_t originMacAddress[6];
  uint8_t targetMacAddress[6];
  uint8_t lastHopMacAddress[6];
  uint8_t data[12];
  uint8_t hopCount;
  uint32_t epochNum;              // Boot count of origin node (replay protection)
  uint16_t seqNum;                // Per-boot message counter (replay protection)
  uint8_t enrollmentPublicKey[32]; // Curve25519 key; zero for non-enrollment messages
};
// 1+1+4+6+6+6+12+1+4+2+32 = 75 bytes (adapter_types is int32_t = 4B, packed)
static_assert(sizeof(mesh_message) == 75, "mesh_message size changed — update server proto");
```

- [ ] **Step 3: Fix MESH_TYPE_JOIN_ACK references in Mesh.cpp**

Search for any place that used the value `3` or compared against the old JOIN_ACK:
```bash
grep -n "MESH_TYPE_JOIN_ACK\|JOIN_ACK\|== 3\|= 3" main/src/Mesh/Mesh.cpp
```

The `drainRecvQueue()` dispatch (around line 567) uses `case MESH_TYPE_JOIN_ACK:` — this is fine, it references the enum name (not the raw value) so it automatically picks up the new value 4.

The `enrollPeer()` function (around line 1004) sets `ack.messageType = MESH_TYPE_JOIN_ACK` — same, fine.

No raw `3` references should exist; if found, replace with the enum name.

- [ ] **Step 4: Compile**

```bash
arduino-cli compile --fqbn esp32:esp32:esp32da main
```
Expected: success.

- [ ] **Step 5: Commit**

```bash
git add main/src/Mesh/Mesh.h main/src/Mesh/Mesh.cpp
git commit -m "feat: mesh_message +enrollmentPublicKey[32] (75B), JOIN_ACK=4, add SERIAL_CMD_BROADCAST=3"
```

---

### Task 3: Replace Serial_Adapter codec with nanopb

**Files:**
- Modify: `main/src/Adapter/Serial_Adapter/Serial_Adapter.h` — remove hand-rolled helpers; add nanopb includes; replace enrollment opcode constants
- Modify: `main/src/Adapter/Serial_Adapter/Serial_Adapter.cpp` — remove helper impls; replace encodeMeshMessage, decodeMeshMessage, relayEnrollmentToServer; update handleCompleteFrame

**Interfaces:**
- Consumes: `MeshMessage`, `MeshMessage_fields`, `pb_encode`, `pb_decode` from Task 1; `mesh_message::enrollmentPublicKey` from Task 2
- Produces: `encodeMeshMessage()`, `decodeMeshMessage()`, `relayEnrollmentToServer()` using nanopb; `handleCompleteFrame()` dispatching JOIN_ACK (type=4)

- [ ] **Step 1: Update Serial_Adapter.h — remove hand-rolled codec, add nanopb**

Replace the entire `Serial_Adapter.h` header. Read the current file first to identify line ranges. The changes are:

**Remove** these constants (raw binary enrollment protocol):
```cpp
static constexpr uint8_t OP_ENROLLMENT_REQ    = 0xC0;
static constexpr uint8_t OP_ENROLLMENT_APPROVE = 0xC1;
static constexpr uint8_t OP_ENROLLMENT_REJECT  = 0xC2;
```

**Add** after `#include "../../project_config.h"` (at top of file):
```cpp
#include "src/Mesh/serialization/nanopb/pb.h"
#include "src/Mesh/serialization/nanopb/pb_encode.h"
#include "src/Mesh/serialization/nanopb/pb_decode.h"
#include "src/Mesh/serialization/mesh.pb.h"
```

**Add** to public constants (near `SERIAL_MSG_BROADCAST`):
```cpp
// messageType == 4 (JOIN_ACK): server approved or rejected enrollment
static constexpr uint32_t SERIAL_MSG_JOIN_ACK = 4;
```

**Remove** all private hand-rolled codec declarations:
```cpp
// REMOVE all of these from the private section:
static size_t writeVarint(uint8_t* out, uint32_t value);
static size_t writeZigZag32(uint8_t* out, int32_t value);
static size_t writeKey(uint8_t* out, uint32_t fieldNumber, uint8_t wireType);
static size_t writeBytesField(uint8_t* out, uint32_t fieldNumber, const uint8_t* data, size_t len);
static size_t writeSint32Field(uint8_t* out, uint32_t fieldNumber, int32_t value);
static size_t writeUint32Field(uint8_t* out, uint32_t fieldNumber, uint32_t value);
static bool readVarint(const uint8_t*& ptr, const uint8_t* end, uint32_t& out);
static bool readZigZag32(const uint8_t*& ptr, const uint8_t* end, int32_t& out);
static bool readKey(const uint8_t*& ptr, const uint8_t* end, uint32_t& fieldNumber, uint8_t& wireType);
static bool readLengthDelimited(const uint8_t*& ptr, const uint8_t* end, const uint8_t*& dataPtr, size_t& dataLen);
```

Keep `encodeMeshMessage` and `decodeMeshMessage` declarations (same signatures, just re-implemented).

- [ ] **Step 2: Replace encodeMeshMessage in Serial_Adapter.cpp**

Remove the old `encodeMeshMessage` implementation (currently lines ~285-386). Replace with:

```cpp
size_t Serial_Adapter::encodeMeshMessage(const planetopia::mesh::mesh_message& msg,
                                         uint8_t* out, size_t outCap) {
  MeshMessage pbMsg = MeshMessage_init_zero;
  pbMsg.messageType  = static_cast<uint32_t>(msg.messageType);
  pbMsg.dataType     = static_cast<int32_t>(msg.dataType);
  pbMsg.hopCount     = msg.hopCount;
  pbMsg.epochNum     = msg.epochNum;
  pbMsg.seqNum       = static_cast<uint32_t>(msg.seqNum);
  pbMsg.protoVersion = static_cast<uint32_t>(msg.protoVersion);
  memcpy(pbMsg.originMacAddress,  msg.originMacAddress,  6);
  memcpy(pbMsg.targetMacAddress,  msg.targetMacAddress,  6);
  memcpy(pbMsg.lastHopMacAddress, msg.lastHopMacAddress, 6);
  pbMsg.data_count = sizeof(msg.data);  // 12 bytes always
  memcpy(pbMsg.data, msg.data, sizeof(msg.data));

  if (msg.messageType == planetopia::mesh::MeshMessageType::MESH_TYPE_ENROLLMENT ||
      msg.messageType == planetopia::mesh::MeshMessageType::MESH_TYPE_JOIN_ACK) {
    bool nonZero = false;
    for (int i = 0; i < 32; ++i) {
      if (msg.enrollmentPublicKey[i]) { nonZero = true; break; }
    }
    if (nonZero) {
      pbMsg.has_public_key = true;
      memcpy(pbMsg.public_key, msg.enrollmentPublicKey, 32);
    }
  }

  pb_ostream_t stream = pb_ostream_from_buffer(out, outCap);
  if (!pb_encode(&stream, MeshMessage_fields, &pbMsg)) {
    return 0;
  }
  return stream.bytes_written;
}
```

- [ ] **Step 3: Replace decodeMeshMessage in Serial_Adapter.cpp**

Remove the old `decodeMeshMessage` implementation (currently lines ~388-513). Replace with:

```cpp
bool Serial_Adapter::decodeMeshMessage(const uint8_t* data, size_t len,
                                       planetopia::mesh::mesh_message& outMsg) {
  memset(&outMsg, 0, sizeof(outMsg));

  MeshMessage pbMsg = MeshMessage_init_zero;
  pb_istream_t stream = pb_istream_from_buffer(data, len);
  if (!pb_decode(&stream, MeshMessage_fields, &pbMsg)) {
    return false;
  }

  outMsg.messageType  = static_cast<planetopia::mesh::MeshMessageType>(pbMsg.messageType);
  outMsg.dataType     = static_cast<planetopia::adapter::adapter_types>(pbMsg.dataType);
  outMsg.hopCount     = static_cast<uint8_t>(pbMsg.hopCount);
  outMsg.epochNum     = pbMsg.epochNum;
  outMsg.seqNum       = static_cast<uint16_t>(pbMsg.seqNum);
  outMsg.protoVersion = static_cast<uint8_t>(pbMsg.protoVersion);
  memcpy(outMsg.targetMacAddress, pbMsg.targetMacAddress, 6);
  size_t dataToCopy = pbMsg.data_count < 12u ? pbMsg.data_count : 12u;
  memcpy(outMsg.data, pbMsg.data, dataToCopy);

  if (pbMsg.has_public_key) {
    memcpy(outMsg.enrollmentPublicKey, pbMsg.public_key, 32);
  }

  readOwnMac(outMsg.originMacAddress);
  readOwnMac(outMsg.lastHopMacAddress);
  return true;
}
```

- [ ] **Step 4: Replace relayEnrollmentToServer in Serial_Adapter.cpp**

Remove the old implementation (lines ~515-527). Replace with:

```cpp
void Serial_Adapter::relayEnrollmentToServer(const uint8_t mac[6], const uint8_t pubKey[32]) {
  planetopia::mesh::mesh_message msg = {};
  msg.messageType  = planetopia::mesh::MeshMessageType::MESH_TYPE_ENROLLMENT;
  msg.protoVersion = 1;
  memcpy(msg.originMacAddress,    mac,    6);
  memcpy(msg.enrollmentPublicKey, pubKey, 32);

  uint8_t encoded[128];
  size_t n = encodeMeshMessage(msg, encoded, sizeof(encoded));
  if (n == 0) return;

  uint8_t lenLE[2] = {
    static_cast<uint8_t>(n & 0xFF),
    static_cast<uint8_t>((n >> 8) & 0xFF)
  };
  Serial.write(lenLE, 2);
  Serial.write(encoded, n);
}
```

- [ ] **Step 5: Update handleCompleteFrame in Serial_Adapter.cpp**

`handleCompleteFrame` starts at line ~529. Make two changes:

**Change 1 — Remove the raw opcode block at the top of handleCompleteFrame:**

Remove this entire block (currently the first thing in the function body):
```cpp
if (len >= 1) {
    uint8_t op = data[0];
    if (op == OP_ENROLLMENT_APPROVE && len >= 39) {
        ...
    } else if (op == OP_ENROLLMENT_REJECT) {
        ...
    }
}
```

**Change 2 — Add JOIN_ACK dispatch after decodeMeshMessage succeeds:**

After the `if (!decodeMeshMessage(...)) return;` line and before the `if (msg.messageType == SERIAL_MSG_BROADCAST)` check, add:

```cpp
  // JOIN_ACK (type=4): server responded to an enrollment request
  if (msg.messageType == planetopia::mesh::MeshMessageType::MESH_TYPE_JOIN_ACK) {
    if (pbMsg.has_public_key) {
      // non-zero key = approved; zero/absent = rejected (no action)
      planetopia::mesh::Mesh* meshInstance = planetopia::mesh::Mesh::getInstance();
      if (meshInstance) {
        meshInstance->enrollPeer(msg.originMacAddress, msg.enrollmentPublicKey);
      }
    }
    return;
  }
```

Note: `pbMsg` is the local `MeshMessage` decoded struct. Either keep it in scope (declare it before the `decodeMeshMessage` call) or re-check `msg.enrollmentPublicKey` for non-zero (since `decodeMeshMessage` only fills it when `has_public_key` is true, checking non-zero is equivalent).

The simplest approach: check `msg.enrollmentPublicKey` directly (it was zeroed by `memset` in `decodeMeshMessage`, then filled only if `has_public_key` was true):

```cpp
  if (msg.messageType == planetopia::mesh::MeshMessageType::MESH_TYPE_JOIN_ACK) {
    bool hasKey = false;
    for (int i = 0; i < 32; ++i) {
      if (msg.enrollmentPublicKey[i]) { hasKey = true; break; }
    }
    if (hasKey) {
      planetopia::mesh::Mesh* meshInstance = planetopia::mesh::Mesh::getInstance();
      if (meshInstance) {
        meshInstance->enrollPeer(msg.originMacAddress, msg.enrollmentPublicKey);
      }
    }
    return;
  }
```

- [ ] **Step 6: Remove hand-rolled helper implementations from Serial_Adapter.cpp**

Delete all implementations from `writeVarint` through `readLengthDelimited` (currently lines ~190-283). These are no longer declared and will cause linker errors if left.

- [ ] **Step 7: Compile**

```bash
arduino-cli compile --fqbn esp32:esp32:esp32da main
```
Expected: success (no undefined references, no redeclarations).

If you see `pb_encode` or `pb_decode` undefined: verify the nanopb `.c` files are picked up by the Arduino build. Arduino auto-includes all `.c` and `.cpp` files under `main/` — the nanopb files at `main/src/Mesh/serialization/nanopb/*.c` should be compiled automatically.

If you see `MeshMessage_fields` undefined: verify `mesh.pb.c` is present at `main/src/Mesh/serialization/mesh.pb.c`.

- [ ] **Step 8: Commit**

```bash
git add main/src/Adapter/Serial_Adapter/Serial_Adapter.h \
        main/src/Adapter/Serial_Adapter/Serial_Adapter.cpp
git commit -m "feat: replace hand-rolled Serial_Adapter codec with nanopb; fix enrollment relay; JOIN_ACK dispatch"
```

---

### Task 4: Host test build + delete dead code

**Files:**
- Delete: `main/src/Mesh/serialization/ProtobufCodec.cpp`
- Delete: `main/src/Mesh/serialization/ProtobufCodec.h`
- Delete: `tests/unit/test_protobuf_codec.cpp`
- Modify: `tests/CMakeLists.txt` — swap ProtobufCodec for nanopb sources
- Modify: `tests/unit/test_serial_framing.cpp` — replace with nanopb round-trip tests

**Interfaces:**
- Consumes: `encodeMeshMessage`, `decodeMeshMessage` from Task 3; `MeshMessage_fields`, `pb_encode`, `pb_decode` from Task 1
- Produces: passing host test suite (at minimum the 6 existing framing tests replaced by nanopb tests)

- [ ] **Step 1: Delete dead ProtobufCodec files**

```bash
git rm main/src/Mesh/serialization/ProtobufCodec.cpp \
       main/src/Mesh/serialization/ProtobufCodec.h \
       tests/unit/test_protobuf_codec.cpp
```

- [ ] **Step 2: Update tests/CMakeLists.txt**

In the `FIRMWARE_SOURCES` list, make these changes:

**Remove:**
```cmake
../main/src/Mesh/serialization/ProtobufCodec.cpp
```

**Add:**
```cmake
../main/src/Mesh/serialization/mesh.pb.c
../main/src/Mesh/serialization/nanopb/pb_encode.c
../main/src/Mesh/serialization/nanopb/pb_decode.c
../main/src/Mesh/serialization/nanopb/pb_common.c
```

In the `include_directories()` block, add:
```cmake
${CMAKE_CURRENT_SOURCE_DIR}/../main/src/Mesh/serialization/nanopb
${CMAKE_CURRENT_SOURCE_DIR}/../main/src/Mesh/serialization
```

Remove the `add_unit_test` line for test_protobuf_codec:
```cmake
# Remove this line:
add_unit_test(test_protobuf_codec  unit/test_protobuf_codec.cpp)
```

- [ ] **Step 3: Verify host build compiles after CMakeLists change**

```bash
cd /Users/benjamin.swanepoel/projects/personal/Planetopia-nodes/tests
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```
Expected: success. If you see `PB_PROTO_HEADER_VERSION` or similar nanopb errors, check that the include paths for `nanopb/` are in `include_directories()`.

- [ ] **Step 4: Write nanopb round-trip tests in test_serial_framing.cpp**

Replace the full content of `tests/unit/test_serial_framing.cpp` with:

```cpp
#include <gtest/gtest.h>
#include <cstring>
#include "src/Mesh/serialization/mesh.pb.h"
#include "src/Mesh/serialization/nanopb/pb_encode.h"
#include "src/Mesh/serialization/nanopb/pb_decode.h"

// Helper: encode a MeshMessage to a buffer, return bytes written (0 = failure)
static size_t encodeMsg(const MeshMessage& pbMsg, uint8_t* out, size_t outCap) {
  pb_ostream_t stream = pb_ostream_from_buffer(out, outCap);
  if (!pb_encode(&stream, MeshMessage_fields, &pbMsg)) return 0;
  return stream.bytes_written;
}

// Helper: decode bytes into a MeshMessage, return true on success
static bool decodeMsg(const uint8_t* data, size_t len, MeshMessage& pbMsg) {
  pbMsg = MeshMessage_init_zero;
  pb_istream_t stream = pb_istream_from_buffer(data, len);
  return pb_decode(&stream, MeshMessage_fields, &pbMsg);
}

// --- ADAPTER_DATA round-trip ---
TEST(NanopbCodec, RoundTrip_AdapterData) {
  MeshMessage enc = MeshMessage_init_zero;
  enc.messageType = 0;  // ADAPTER_DATA
  enc.dataType    = 0;  // PIR
  enc.hopCount    = 2;
  enc.epochNum    = 7;
  enc.seqNum      = 42;
  enc.protoVersion = 1;

  const uint8_t origin[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
  memcpy(enc.originMacAddress, origin, 6);

  enc.data_count = 12;
  for (int i = 0; i < 12; ++i) enc.data[i] = static_cast<uint8_t>(0x10 + i);

  uint8_t buf[256];
  size_t n = encodeMsg(enc, buf, sizeof(buf));
  ASSERT_GT(n, 0u);

  MeshMessage dec;
  ASSERT_TRUE(decodeMsg(buf, n, dec));
  EXPECT_EQ(dec.messageType, 0u);
  EXPECT_EQ(dec.dataType, 0);
  EXPECT_EQ(dec.hopCount, 2u);
  EXPECT_EQ(dec.epochNum, 7u);
  EXPECT_EQ(dec.seqNum, 42u);
  EXPECT_EQ(dec.protoVersion, 1u);
  EXPECT_EQ(memcmp(dec.originMacAddress, origin, 6), 0);
  EXPECT_EQ(dec.data_count, 12u);
  for (int i = 0; i < 12; ++i) EXPECT_EQ(dec.data[i], 0x10 + i) << "data[" << i << "]";
  EXPECT_FALSE(dec.has_public_key);
}

// --- ENROLLMENT with public key ---
TEST(NanopbCodec, RoundTrip_Enrollment_WithKey) {
  MeshMessage enc = MeshMessage_init_zero;
  enc.messageType  = 2;  // ENROLLMENT
  enc.protoVersion = 1;

  const uint8_t mac[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
  memcpy(enc.originMacAddress, mac, 6);

  enc.has_public_key = true;
  for (int i = 0; i < 32; ++i) enc.public_key[i] = static_cast<uint8_t>(i + 1);

  enc.data_count = 12;
  memset(enc.data, 0, 12);

  uint8_t buf[256];
  size_t n = encodeMsg(enc, buf, sizeof(buf));
  ASSERT_GT(n, 0u);

  MeshMessage dec;
  ASSERT_TRUE(decodeMsg(buf, n, dec));
  EXPECT_EQ(dec.messageType, 2u);
  EXPECT_EQ(dec.protoVersion, 1u);
  EXPECT_EQ(memcmp(dec.originMacAddress, mac, 6), 0);
  ASSERT_TRUE(dec.has_public_key);
  for (int i = 0; i < 32; ++i) EXPECT_EQ(dec.public_key[i], i + 1) << "key[" << i << "]";
}

// --- JOIN_ACK with no key = rejection ---
TEST(NanopbCodec, RoundTrip_JoinAck_Rejected) {
  MeshMessage enc = MeshMessage_init_zero;
  enc.messageType  = 4;  // JOIN_ACK
  enc.protoVersion = 1;

  const uint8_t mac[6] = {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x01};
  memcpy(enc.originMacAddress, mac, 6);
  enc.has_public_key = false;  // no key = rejection

  enc.data_count = 12;
  memset(enc.data, 0, 12);

  uint8_t buf[256];
  size_t n = encodeMsg(enc, buf, sizeof(buf));
  ASSERT_GT(n, 0u);

  MeshMessage dec;
  ASSERT_TRUE(decodeMsg(buf, n, dec));
  EXPECT_EQ(dec.messageType, 4u);
  EXPECT_EQ(memcmp(dec.originMacAddress, mac, 6), 0);
  EXPECT_FALSE(dec.has_public_key);
}

// --- Truncated buffer → decode fails ---
TEST(NanopbCodec, TruncatedBuffer_DecodeFails) {
  MeshMessage enc = MeshMessage_init_zero;
  enc.messageType = 0;
  enc.data_count = 12;
  memset(enc.data, 0xAB, 12);

  uint8_t buf[256];
  size_t n = encodeMsg(enc, buf, sizeof(buf));
  ASSERT_GT(n, 4u);  // must have some bytes to truncate

  MeshMessage dec;
  // Provide only half the bytes
  EXPECT_FALSE(decodeMsg(buf, n / 2, dec));
}

// --- Empty buffer → decode fails ---
TEST(NanopbCodec, EmptyBuffer_DecodeFails) {
  MeshMessage dec;
  EXPECT_FALSE(decodeMsg(nullptr, 0, dec));
}

// --- sint32 zigzag: UNKNOWN_ADAPTER = -1 survives round-trip ---
TEST(NanopbCodec, ZigZag_UnknownAdapter) {
  MeshMessage enc = MeshMessage_init_zero;
  enc.messageType = 0;
  enc.dataType    = -1;  // UNKNOWN_ADAPTER
  enc.data_count  = 12;
  memset(enc.data, 0, 12);

  uint8_t buf[256];
  size_t n = encodeMsg(enc, buf, sizeof(buf));
  ASSERT_GT(n, 0u);

  MeshMessage dec;
  ASSERT_TRUE(decodeMsg(buf, n, dec));
  EXPECT_EQ(dec.dataType, -1);
}
```

- [ ] **Step 5: Run all tests**

```bash
cd /Users/benjamin.swanepoel/projects/personal/Planetopia-nodes/tests/build
ctest --output-on-failure
```
Expected: all tests pass, including the 6 new `NanopbCodec.*` tests.

- [ ] **Step 6: Arduino compile still passes**

```bash
arduino-cli compile --fqbn esp32:esp32:esp32da main
```
Expected: success.

- [ ] **Step 7: Commit**

```bash
cd /Users/benjamin.swanepoel/projects/personal/Planetopia-nodes
git add tests/CMakeLists.txt \
        tests/unit/test_serial_framing.cpp
git commit -m "test: replace ProtobufCodec tests with nanopb round-trip tests; update CMake for nanopb"

# Then delete the dead files
git rm main/src/Mesh/serialization/ProtobufCodec.cpp \
       main/src/Mesh/serialization/ProtobufCodec.h \
       tests/unit/test_protobuf_codec.cpp
git commit -m "refactor: delete dead ProtobufCodec (replaced by nanopb)"
```

---

## Server-Side Spec

Server changes are documented separately in `docs/superpowers/specs/2026-06-25-nanopb-server-changes.md` and must be implemented in the `motionSensorServer` repo. The firmware can be deployed without the server update — existing ADAPTER_DATA and MASTER_BEACON flows are unaffected (proto3 unknown fields are ignored). Enrollment will not work end-to-end until the server adds its enrollment handler.
