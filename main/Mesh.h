#ifndef MESH_H
#define MESH_H
#include <functional>
#include <esp_now.h>
#include <WiFi.h>
#include <Arduino.h>
#include <esp_wifi.h>

enum mesh_message_types {
  PIR_SENSOR_MESSAGE_TYPE,
  WIFI_MESSAGE_TYPE,
  LED_MESSAGE_TYPE,
};

typedef struct mesh_message {
  uint8_t originMacAddress[6];
  enum mesh_message_types dataType;
  byte data[12];
};

class Mesh {
private:
  static Mesh *instance;

  // REPLACE WITH THE MAC Address of your receiver
  static uint8_t broadcastAddress[6];
  //static uint8_t deviceMacAddress[] = { 0xEC, 0x64, 0xC9, 0x5D, 0x22, 0x20 };
  uint8_t deviceMacAddress[6];

  void readMacAddress();
  void printMac(uint8_t mac[6]);

  void printMeshMessage(const mesh_message& msg);

  // Create a message to hold incoming sensor readings
  esp_now_peer_info_t peerInfo;

  static void onDataSentCallback(const uint8_t *mac_addr, esp_now_send_status_t status);

  void onDataRecvCallback(const esp_now_recv_info *mac, const uint8_t *incomingData, int len);
  std::function<void(mesh_message)> externalRecvCallback;
  static void dataRecvTrampoline(const esp_now_recv_info *mac_addr, const uint8_t *data, int len);

public:
  Mesh();
  void init();
  void transmit(mesh_message dataToTransmit);
  void linkDataRecvCallback(std::function<void(mesh_message incomingMessage)> recvCallback);
};
#endif