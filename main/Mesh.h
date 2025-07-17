#ifndef MESH_H
#define MESH_H
#include <esp_now.h>
#include <WiFi.h>
#include <Arduino.h>
#include <esp_wifi.h>

class Mesh{
private:
  // REPLACE WITH THE MAC Address of your receiver
  uint8_t broadcastAddress[6];
  uint8_t deviceMacAddress[6];
  uint8_t *readMacAddress();

  // Create a message to hold incoming sensor readings
  message dataToReceive;
  esp_now_peer_info_t peerInfo;

  void OnDataSentCallback(const uint8_t *mac_addr, esp_now_send_status_t status); 
  void OnDataRecvCallback(const esp_now_recv_info *mac, const uint8_t *incomingData, int len);

public:
    enum messageTypes {
        PIR_SENSOR_MESSAGE_TYPE,
        WIFI_MESSAGE_TYPE,
        LED_MESSAGE_TYPE,
    };

    typedef struct message {
        uint8_t originMacAddress[6];
        enum messageTypes dataType;
        byte data[12];
    } message;

    Mesh()

    void init();
    void transmit(message dataToTransmit);

}



#endif
