#include "Serial_Adapter.h"
#include "src/Adapter/AdapterFactory.h"
#include "src/utils/Logger.h"
#include "src/utils/ErrorHandler.h"
#include "src/Mesh/Mesh.h"
#include <cstring>
#include <EEPROM.h>

namespace planetopia {
namespace adapter {

using namespace planetopia::utils;


unsigned long Serial_Adapter::lastHealthMillis = 0;

static void readOwnMac(uint8_t out[6]) {
  esp_wifi_get_mac(WIFI_IF_STA, out);
}

void Serial_Adapter::sendHealthReport() {
  uint8_t data[12] = { 0 };
  data[0] = OP_HEALTH_REPORT;
  data[1] = static_cast<uint8_t>(SERIAL_ADAPTER);
  uint8_t mac[6];
  readOwnMac(mac);
  memcpy(&data[2], mac, 6);
  uint32_t uptimeSec = millis() / 1000;
  data[8] = static_cast<uint8_t>(uptimeSec & 0xFF);
  data[9] = static_cast<uint8_t>((uptimeSec >> 8) & 0xFF);
  data[10] = static_cast<uint8_t>((uptimeSec >> 16) & 0xFF);
  data[11] = static_cast<uint8_t>((uptimeSec >> 24) & 0xFF);
  if (planetopia::mesh::Mesh::transmit) {
    planetopia::mesh::Mesh::transmit(SERIAL_ADAPTER, data);
  }
}

Serial_Adapter::Serial_Adapter(int pin)
  : Adapter(pin), frameState(FrameState::AwaitingLen1), frameLength(0), frameIndex(0) {
  _adapterType = SERIAL_ADAPTER;
  memset(payloadBuffer, 0, sizeof(payloadBuffer));
}

bool Serial_Adapter::init() {
  // Serial already initialized in main. Nothing to do.
  Logger::logln("Serial_Adapter", "Initialized", LogLevel::LOG_INFO);
  return true;
}

void Serial_Adapter::loop() {
  // periodic health
  if (millis() - lastHealthMillis > 5000) {
    lastHealthMillis = millis();
    sendHealthReport();
  }
  while (Serial.available() > 0) {
    uint8_t byteIn = static_cast<uint8_t>(Serial.read());
    switch (frameState) {
      case FrameState::AwaitingLen1:
        frameLength = byteIn;
        frameState = FrameState::AwaitingLen2;
        break;
      case FrameState::AwaitingLen2:
        frameLength |= static_cast<uint16_t>(byteIn) << 8;
        if (frameLength == 0 || frameLength > MAX_PAYLOAD) {
          // Reset on invalid length
          frameState = FrameState::AwaitingLen1;
          frameLength = 0;
          frameIndex = 0;
        } else {
          frameIndex = 0;
          frameState = FrameState::AwaitingPayload;
        }
        break;
      case FrameState::AwaitingPayload:
        payloadBuffer[frameIndex++] = byteIn;
        if (frameIndex >= frameLength) {
          handleCompleteFrame(payloadBuffer, frameLength);
          frameState = FrameState::AwaitingLen1;
          frameLength = 0;
          frameIndex = 0;
        }
        break;
    }
  }
}

void Serial_Adapter::onMeshDataImpl(const planetopia::mesh::mesh_message& message) {
  uint8_t encoded[MAX_PAYLOAD];
  size_t n = encodeMeshMessage(message, encoded, sizeof(encoded));
  if (n == 0) return;
  // 2-byte little-endian length prefix
  uint8_t lenLE[2] = { static_cast<uint8_t>(n & 0xFF), static_cast<uint8_t>((n >> 8) & 0xFF) };
  Serial.write(lenLE, 2);
  Serial.write(encoded, n);
}
// --- Minimal Protobuf encoding helpers ---
size_t Serial_Adapter::writeVarint(uint8_t* out, uint32_t value) {
  size_t i = 0;
  while (value >= 0x80) {
    out[i++] = static_cast<uint8_t>((value & 0x7F) | 0x80);
    value >>= 7;
  }
  out[i++] = static_cast<uint8_t>(value);
  return i;
}

size_t Serial_Adapter::writeZigZag32(uint8_t* out, int32_t value) {
  uint32_t zigzag = (static_cast<uint32_t>(value) << 1) ^ static_cast<uint32_t>(value >> 31);
  return writeVarint(out, zigzag);
}

size_t Serial_Adapter::writeKey(uint8_t* out, uint32_t fieldNumber, uint8_t wireType) {
  return writeVarint(out, (fieldNumber << 3) | (wireType & 0x07));
}

size_t Serial_Adapter::writeBytesField(uint8_t* out, uint32_t fieldNumber, const uint8_t* data, size_t len) {
  size_t idx = 0;
  idx += writeKey(out + idx, fieldNumber, 2);
  idx += writeVarint(out + idx, static_cast<uint32_t>(len));
  memcpy(out + idx, data, len);
  idx += len;
  return idx;
}

size_t Serial_Adapter::writeSint32Field(uint8_t* out, uint32_t fieldNumber, int32_t value) {
  size_t idx = 0;
  idx += writeKey(out + idx, fieldNumber, 0);
  idx += writeZigZag32(out + idx, value);
  return idx;
}

size_t Serial_Adapter::writeUint32Field(uint8_t* out, uint32_t fieldNumber, uint32_t value) {
  size_t idx = 0;
  idx += writeKey(out + idx, fieldNumber, 0);
  idx += writeVarint(out + idx, value);
  return idx;
}

bool Serial_Adapter::readVarint(const uint8_t*& ptr, const uint8_t* end, uint32_t& out) {
  uint32_t result = 0;
  int shift = 0;
  while (ptr < end && shift <= 28) {
    uint8_t byte = *ptr++;
    result |= static_cast<uint32_t>(byte & 0x7F) << shift;
    if ((byte & 0x80) == 0) {
      out = result;
      return true;
    }
    shift += 7;
  }
  return false;
}

bool Serial_Adapter::readZigZag32(const uint8_t*& ptr, const uint8_t* end, int32_t& out) {
  uint32_t u = 0;
  if (!readVarint(ptr, end, u)) return false;
  out = static_cast<int32_t>((u >> 1) ^ (~(u & 1) + 1));
  return true;
}

bool Serial_Adapter::readKey(const uint8_t*& ptr, const uint8_t* end, uint32_t& fieldNumber, uint8_t& wireType) {
  uint32_t key = 0;
  if (!readVarint(ptr, end, key)) return false;
  wireType = static_cast<uint8_t>(key & 0x07);
  fieldNumber = key >> 3;
  return true;
}

bool Serial_Adapter::readLengthDelimited(const uint8_t*& ptr, const uint8_t* end, const uint8_t*& dataPtr, size_t& dataLen) {
  uint32_t len = 0;
  if (!readVarint(ptr, end, len)) return false;
  if (ptr + len > end) return false;
  dataPtr = ptr;
  dataLen = len;
  ptr += len;
  return true;
}

size_t Serial_Adapter::encodeMeshMessage(const planetopia::mesh::mesh_message& msg, uint8_t* out, size_t outCap) {
  size_t idx = 0;
  auto ensure = [&](size_t need) {
    return idx + need <= outCap;
  };

  uint8_t tmp[32];
  size_t n;

  // 1: messageType (uint32 varint)
  n = writeUint32Field(tmp, 1, static_cast<uint32_t>(msg.messageType));
  if (!ensure(n)) return 0;
  memcpy(out + idx, tmp, n);
  idx += n;

  // 2: dataType (sint32 zigzag)
  n = writeSint32Field(tmp, 2, static_cast<int32_t>(msg.dataType));
  if (!ensure(n)) return 0;
  memcpy(out + idx, tmp, n);
  idx += n;

  // 3,4,5: MACs as bytes (len 6)
  n = writeBytesField(tmp, 3, msg.originMacAddress, 6);
  if (!ensure(n)) return 0;
  memcpy(out + idx, tmp, n);
  idx += n;
  n = writeBytesField(tmp, 4, msg.targetMacAddress, 6);
  if (!ensure(n)) return 0;
  memcpy(out + idx, tmp, n);
  idx += n;
  n = writeBytesField(tmp, 5, msg.lastHopMacAddress, 6);
  if (!ensure(n)) return 0;
  memcpy(out + idx, tmp, n);
  idx += n;

  // 6: data (len 12)
  n = writeBytesField(tmp, 6, msg.data, 12);
  if (!ensure(n)) return 0;
  memcpy(out + idx, tmp, n);
  idx += n;

  // 7: hopCount (uint32 varint)
  n = writeUint32Field(tmp, 7, static_cast<uint32_t>(msg.hopCount));
  if (!ensure(n)) return 0;
  memcpy(out + idx, tmp, n);
  idx += n;

  return idx;
}

bool Serial_Adapter::decodeMeshMessage(const uint8_t* data, size_t len, planetopia::mesh::mesh_message& outMsg) {
  memset(&outMsg, 0, sizeof(outMsg));
  const uint8_t* ptr = data;
  const uint8_t* end = data + len;

  // Auto-generate routing fields that the server doesn't need to send
  // This simplifies the server implementation - it only needs to send essential fields
  readOwnMac(outMsg.originMacAddress);  // Set origin to this node's MAC
  outMsg.hopCount = 0;  // Start at 0 hops for serial-originated messages

  while (ptr < end) {
    uint32_t field = 0;
    uint8_t wt = 0;
    if (!readKey(ptr, end, field, wt)) return false;
    if (field == 0) return false;

    switch (field) {
      case 1:
        {  // messageType: varint
          if (wt != 0) return false;
          uint32_t v = 0;
          if (!readVarint(ptr, end, v)) return false;
          outMsg.messageType = static_cast<planetopia::mesh::MeshMessageType>(v);
          break;
        }
      case 2:
        {  // dataType: sint32
          if (wt != 0) return false;
          int32_t v = 0;
          if (!readZigZag32(ptr, end, v)) return false;
          outMsg.dataType = static_cast<planetopia::adapter::adapter_types>(v);
          break;
        }
      case 4:  // targetMacAddress (only field the server needs to send)
        {
          if (wt != 2) return false;
          const uint8_t* p = nullptr;
          size_t l = 0;
          if (!readLengthDelimited(ptr, end, p, l)) return false;
          memset(outMsg.targetMacAddress, 0, 6);
          if (l > 0) memcpy(outMsg.targetMacAddress, p, l > 6 ? 6 : l);
          break;
        }
      case 6:
        {  // data
          if (wt != 2) return false;
          const uint8_t* p = nullptr;
          size_t l = 0;
          if (!readLengthDelimited(ptr, end, p, l)) return false;
          memset(outMsg.data, 0, 12);
          if (l > 0) memcpy(outMsg.data, p, l > 12 ? 12 : l);
          break;
        }
      default:
        {
          // Skip unknown fields (including 3: originMacAddress, 5: lastHopMacAddress, 7: hopCount)
          if (wt == 0) {
            uint32_t dummy;
            if (!readVarint(ptr, end, dummy)) return false;
          } else if (wt == 2) {
            const uint8_t* p;
            size_t l;
            if (!readLengthDelimited(ptr, end, p, l)) return false;
          } else {
            return false;
          }
          break;
        }
    }
  }
  
  // Set lastHopMacAddress to this node's MAC (since we're the origin)
  readOwnMac(outMsg.lastHopMacAddress);
  
  return true;
}

void Serial_Adapter::handleCompleteFrame(const uint8_t* data, size_t len) {
  planetopia::mesh::mesh_message msg;
  if (!decodeMeshMessage(data, len, msg)) {
    ErrorHandler::getInstance().signalError(ErrorType::COMMUNICATION_FAIL, "Serial_Adapter: Failed to decode protobuf frame");
    return;
  }

  // Only forward adapter data via mesh transmit function; routing fields are managed by Mesh
  if (msg.messageType == planetopia::mesh::MESH_TYPE_ADAPTER_DATA) {
    if (mesh_transmit_fn) {
      // Targeted send via normal mesh transmit path (to master, route onward)
      mesh_transmit_fn(msg.dataType, msg.data);
    } else {
      ErrorHandler::getInstance().signalError(ErrorType::CONFIG_ERROR, "Serial_Adapter: transmit function not set");
    }
  } else if (msg.messageType == SERIAL_MSG_BROADCAST) {
    // Broadcast adapter data to all peers
    planetopia::mesh::Mesh::broadcastAdapterDataStatic(msg.dataType, msg.data);
  }

  // Handle control opcodes: CONFIG_SET, HEALTH_REQ
  if (msg.dataType == SERIAL_ADAPTER) {
    uint8_t op = msg.data[0];
    if (op == OP_HEALTH_REQ) {
      sendHealthReport();
    } else if (op == OP_CONFIG_SET) {
      // Apply only if targeted to me (or broadcast FF:..:FF)
      uint8_t myMac[6];
      readOwnMac(myMac);
      bool allFF = true;
      for (int i = 0; i < 6; ++i)
        if (msg.targetMacAddress[i] != 0xFF) {
          allFF = false;
          break;
        }
      bool isTarget = allFF || (memcmp(msg.targetMacAddress, myMac, 6) == 0);
      if (isTarget) {
        adapter_types newType = static_cast<adapter_types>(static_cast<int8_t>(msg.data[7]));
        planetopia::adapter::AdapterFactory::saveAdapterTypeToEEPROM(newType);
        // Pin is automatically inferred from adapter type - no need to store it
        // Let main recreate adapter on next boot or we could soft-switch by signaling error-led+restart
        ESP.restart();
      }
    }
  }
}

}  // namespace adapter
}  // namespace planetopia
