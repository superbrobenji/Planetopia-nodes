#ifndef PEER_MANAGER_H
#define PEER_MANAGER_H

#include <vector>
#include <esp_now.h>
#include <cstdint>
#include "src/Mesh/Mesh.h"
#include "src/persistence/EEPROM_Manager.h"
#include "src/core/Logger.h"
#include "src/error/Error.h"

using planetopia::mesh::PeerInfo;
using planetopia::mesh::MasterInfo;

namespace planetopia {
namespace utils {

class PeerManager {
public:
  // Peer status
  enum class PeerStatus {
    UNKNOWN,
    ONLINE,
    OFFLINE,
    STALE
  };

  // Peer discovery result
  struct DiscoveryResult {
    bool success;
    uint8_t mac[6];
    uint8_t signalStrength;
    String errorMessage;
  };

  // Constructor
  PeerManager();

  // Core peer management
  bool addPeer(const uint8_t mac[6], bool saveToEEPROM = true);
  bool removePeer(const uint8_t mac[6], bool saveToEEPROM = true);
  bool isPeer(const uint8_t mac[6]) const;
  PeerStatus getPeerStatus(const uint8_t mac[6]) const;

  // Peer discovery and monitoring
  DiscoveryResult discoverPeer(const uint8_t mac[6]);
  void updatePeerLastSeen(const uint8_t mac[6]);
  void cleanupStalePeers(uint32_t staleThresholdMs = 8000);

  // EEPROM operations
  bool loadPeersFromEEPROM();
  bool savePeersToEEPROM();
  void clearAllPeers();

  // ESP-NOW integration
  bool addPeerToESPNow(const uint8_t mac[6], const uint8_t* encryptionKey);
  bool removePeerFromESPNow(const uint8_t mac[6]);

  // Getters
  const std::vector<PeerInfo>& getPeerList() const {
    return peers_;
  }
  size_t getPeerCount() const {
    return peers_.size();
  }
  bool hasPeers() const {
    return !peers_.empty();
  }
  bool isPeerListFull() const {
    return peers_.size() >= EEPROM_SIZES::MAX_PEERS;
  }

  // Configuration
  void setStaleThreshold(uint32_t threshold) {
    staleThresholdMs_ = threshold;
  }
  void setMaxPeers(size_t maxPeers) {
    maxPeers_ = maxPeers;
  }

  // Flush any pending peer-list changes to EEPROM if FLUSH_INTERVAL elapsed
  void periodicFlush();

  static constexpr uint32_t FLUSH_INTERVAL_MS = 10000; // 10-s batch interval

private:
  std::vector<PeerInfo> peers_;
  uint32_t staleThresholdMs_;
  size_t maxPeers_;
  uint32_t lastFlushMs_;

  // Internal helper methods
  PeerInfo* findPeer(const uint8_t mac[6]);
  const PeerInfo* findPeer(const uint8_t mac[6]) const;
  bool isValidMac(const uint8_t mac[6]) const;
  void logPeerOperation(const char* operation, const uint8_t mac[6], bool success);
};

}  // namespace utils
}  // namespace planetopia

#endif  // PEER_MANAGER_H
