#ifndef MESH_H
#define MESH_H

#include <functional>
#include <esp_now.h>
#include <WiFi.h>
#include <Arduino.h>
#include <esp_wifi.h>
#include "src/Adapter/Adapter.h"

namespace planetopia {
namespace mesh {
using planetopia::adapter::adapter_types;

enum MeshMessageType : uint8_t {
    MESH_TYPE_ADAPTER_DATA   = 0,
    MESH_TYPE_MASTER_BEACON  = 1,
    // add other mesh types as needed
};

// The extended mesh message struct, now ready for routing/relaying
struct mesh_message {
  MeshMessageType messageType;  // NEW: always present
  uint8_t originMacAddress[6];   // Who started this message
  uint8_t targetMacAddress[6];   // Final destination (master for most cases)
  uint8_t lastHopMacAddress[6];  // Most recent node that relayed (or sender if first hop)
  adapter_types dataType;
  uint8_t data[12];
  uint8_t hopCount;  // For loop prevention/diagnostics
};

// Stores info about discovered master for routing
struct MasterInfo {
  uint8_t mac[6];
  uint8_t distance;
  uint8_t nextHop[6];
};

class Mesh {
private:
  static Mesh* instance;

  // Broadcast MAC address (replace with your receiver MAC)
  static uint8_t broadcastAddress[6];
  static constexpr uint8_t broadcastMac[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
  uint8_t deviceMacAddress[6];

  esp_now_peer_info_t peerInfo;

  void readMacAddress();
  void printMac(const uint8_t mac[6]);
  void printMeshMessage(const mesh_message& msg);

  static void onDataSentCallback(const wifi_tx_info_t* mac_addr, esp_now_send_status_t status);
  void onDataRecvCallback(const esp_now_recv_info* mac, const uint8_t* incomingData, int len);
  static void dataRecvTrampoline(const esp_now_recv_info* mac_addr, const uint8_t* data, int len);

  void transmitCore(const adapter_types type, const uint8_t data[12], const uint8_t targetMac[6]);

  std::function<void(mesh_message)> externalRecvCallback;


  bool isMaster = false;  // Set at runtime: is this node the master?
public:
  MasterInfo currentMaster;  // Info about the master and routing path

  Mesh();
  bool init();

  // Transmit API extended for explicit target
  static void transmit(const adapter_types type, const uint8_t data[12], const uint8_t targetMac[6]);
  static void transmit(const adapter_types type, const uint8_t data[12]);  // fallback: broadcast

  void linkDataRecvCallback(const std::function<void(mesh_message)> recvCallback);

  // Runtime configuration for all-nodes-same-firmware
  void setIsMaster(bool master) {
    isMaster = master;
  }
  bool getIsMaster() const {
    return isMaster;
  }

  // Call periodically if isMaster
  void broadcastMasterBeacon();
};

}
}

#endif  // MESH_H
