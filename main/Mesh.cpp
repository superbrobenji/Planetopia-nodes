#include "Mesh.h"

Mesh *Mesh::instance = nullptr;
uint8_t Mesh::broadcastAddress[] = { 0xEC, 0x64, 0xC9, 0x5D, 0xac, 0x18 };

Mesh::Mesh() {}

void Mesh::printMeshMessage(const mesh_message &msg) {
  Serial.print("MAC: ");
  for (int i = 0; i < 6; i++) {
    if (msg.originMacAddress[i] < 0x10) Serial.print("0");  // leading zero
    Serial.print(msg.originMacAddress[i], HEX);
    if (i < 5) Serial.print(":");
  }
  Serial.println();

  Serial.print("Data Type: ");
  Serial.println(msg.dataType);  // You can switch() here if you want labels

  Serial.print("Data: ");
  for (int i = 0; i < 12; i++) {
    if (msg.data[i] < 0x10) Serial.print("0");
    Serial.print(msg.data[i], HEX);
    Serial.print(" ");
  }
  Serial.println();
}

void Mesh::onDataSentCallback(const uint8_t *mac_addr, esp_now_send_status_t status) {
  Serial.print("\r\nLast Packet Send Status:\t");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Delivery Success" : "Delivery Fail");
  if (status == 0) {
    Serial.print("\r\nDelivery Sucess\t");
  } else {
    Serial.print("\r\nDelivery Fail :(\t");
    // digitalWrite(redLed, HIGH);
    //delay(1000);
    //digitalWrite(redLed, LOW);
  }
}

void Mesh::onDataRecvCallback(const esp_now_recv_info *mac, const uint8_t *incomingData, int len) {
  mesh_message dataToReceive;
  memcpy(&dataToReceive, incomingData, sizeof(dataToReceive));
  Serial.println("Bytes received: ");
  printMeshMessage(dataToReceive);
  if (externalRecvCallback) {
    externalRecvCallback(dataToReceive);
  };

  Serial.println(len);
}

void Mesh::dataRecvTrampoline(const esp_now_recv_info *mac_addr, const uint8_t *data, int len) {
  if (instance) {
    instance->onDataRecvCallback(mac_addr, data, len);
  }
}

void Mesh::readMacAddress() {
  esp_err_t ret = esp_wifi_get_mac(WIFI_IF_STA, deviceMacAddress);
  if (ret != ESP_OK) {
    Serial.println("Failed to read MAC address");
  }
}

void Mesh::printMac(uint8_t mac[6]) {
  for (int i = 0; i < 6; i++) {
    if (mac[i] < 0x10) {
      Serial.print("0");  // Leading zero for values less than 0x10
    }
    Serial.print(mac[i], HEX);
    if (i < 5) {
      Serial.print(":");
    }
  }
  Serial.println();
}

void Mesh::init() {
  instance = this;
  // Set device as a Wi-Fi Station
  WiFi.mode(WIFI_STA);

  readMacAddress();

  // Init ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    //digitalWrite(redLed, HIGH);
    return;
  }

  // Once ESPNow is successfully Init, we will register for Send CB to
  // get the status of Trasnmitted packet
  esp_now_register_send_cb(onDataSentCallback);

  //TODOadd peers dynamically
  // Register peer
  memcpy(peerInfo.peer_addr, broadcastAddress, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;

  // Add peer
  if (!esp_now_is_peer_exist(broadcastAddress)) {
    esp_err_t status = esp_now_add_peer(&peerInfo);
    if (status != ESP_OK) {
      Serial.printf("Peer add failed: %s\n", esp_err_to_name(status));
    }
  }
  // Register for a callback function that will be called when data is received and sent
  esp_now_register_recv_cb(Mesh::dataRecvTrampoline);
}

void Mesh::transmit(mesh_message dataToTransmit) {
  // Send message via ESP-NOW
  memcpy(dataToTransmit.originMacAddress, deviceMacAddress, 6);
  printMeshMessage(dataToTransmit);

  esp_err_t result = esp_now_send(broadcastAddress, (uint8_t *)&dataToTransmit, sizeof(dataToTransmit));
  if (result == ESP_OK) {
    Serial.println("Sent with success");
  } else {
    Serial.print("Error sending the data: ");
    Serial.println(esp_err_to_name(result));
    //digitalWrite(redLed, HIGH);
    //delay(1000);
    //digitalWrite(redLed, LOW);
  }
}

void Mesh::linkDataRecvCallback(std::function<void(mesh_message incomingMessage)> recvCallback) {
  externalRecvCallback = recvCallback;
}