#include <gtest/gtest.h>
#include <cstring>
#include <vector>
#include "Adapter/PIR_Adapter/PIR_Adapter.h"
#include "Adapter/AdapterFactory.h"
#include "esp_wifi_mock.h"
#include "time_mock.h"
#include "EEPROM.h"

using namespace planetopia::adapter;

// Capture buffer for transmitted data
static adapter_types lastTxType;
static std::vector<uint8_t> lastTxData;
static int txCallCount = 0;

static void captureTransmit(adapter_types type, const uint8_t data[64]) {
  lastTxType = type;
  lastTxData.assign(data, data + 64);
  ++txCallCount;
}

class PIRHealthTest : public ::testing::Test {
protected:
  void SetUp() override {
    EEPROM.reset();
    resetMillis();
    resetWifiMock();
    lastTxType = adapter_types::UNKNOWN_ADAPTER;
    lastTxData.clear();
    txCallCount = 0;
  }
};

// Helper: construct + init a PIR_Adapter with the capture transmit fn wired up.
// Pin 27 is valid per GpioInput::isValidInputPin.
static PIR_Adapter* makePir() {
  auto* pir = new PIR_Adapter(27);
  pir->setTransmitFn(captureTransmit);
  pir->init();  // sets PIR_Adapter::instance = pir; also marks _initialized
  return pir;
}

TEST_F(PIRHealthTest, SendsNodeHealthAfter30s) {
  PIR_Adapter* pir = makePir();

  // Advance past the 30s threshold
  advanceMillis(30001);
  pir->loop();

  ASSERT_EQ(txCallCount, 1);
  ASSERT_EQ(lastTxType, adapter_types::SERIAL_ADAPTER);
  ASSERT_EQ(lastTxData.size(), 64u);

  // data[0]: opcode 0xB2
  EXPECT_EQ(lastTxData[0], 0xB2u);
  // data[1]: adapterTypeToEEPROM(PIR_ADAPTER) == 0
  EXPECT_EQ(lastTxData[1], 0x00u);
  // data[2..7]: mockDeviceMac default {0xAA,0xBB,0xCC,0xDD,0xEE,0xFF}
  const uint8_t expectedMac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
  EXPECT_EQ(memcmp(&lastTxData[2], expectedMac, 6), 0);
  // data[8..11]: uptime in seconds (30001ms / 1000 = 30)
  uint32_t uptime = static_cast<uint32_t>(lastTxData[8])
                  | (static_cast<uint32_t>(lastTxData[9])  << 8)
                  | (static_cast<uint32_t>(lastTxData[10]) << 16)
                  | (static_cast<uint32_t>(lastTxData[11]) << 24);
  EXPECT_EQ(uptime, 30u);

  delete pir;
}

TEST_F(PIRHealthTest, DoesNotSendNodeHealthBefore30s) {
  PIR_Adapter* pir = makePir();

  advanceMillis(29999);
  pir->loop();

  EXPECT_EQ(txCallCount, 0);

  delete pir;
}

TEST_F(PIRHealthTest, SendsHealthExactlyAtThreshold) {
  PIR_Adapter* pir = makePir();

  advanceMillis(30000);
  pir->loop();

  EXPECT_EQ(txCallCount, 1);
  EXPECT_EQ(lastTxData[0], 0xB2u);

  delete pir;
}

TEST_F(PIRHealthTest, DoesNotSendHealthTwiceWithinInterval) {
  PIR_Adapter* pir = makePir();

  advanceMillis(30001);
  pir->loop();
  ASSERT_EQ(txCallCount, 1);

  // Advance another 5s (not yet another full interval)
  advanceMillis(5000);
  pir->loop();

  EXPECT_EQ(txCallCount, 1);

  delete pir;
}

TEST_F(PIRHealthTest, SendsHealthAgainAfterSecondInterval) {
  PIR_Adapter* pir = makePir();

  advanceMillis(30001);
  pir->loop();
  ASSERT_EQ(txCallCount, 1);

  // Advance another full interval from when the last health was sent
  advanceMillis(30000);
  pir->loop();

  EXPECT_EQ(txCallCount, 2);

  delete pir;
}

TEST_F(PIRHealthTest, UptimeReflectsActualMillis) {
  PIR_Adapter* pir = makePir();

  // Simulate 2 minutes elapsed
  advanceMillis(120001);
  pir->loop();

  ASSERT_EQ(txCallCount, 1);
  uint32_t uptime = static_cast<uint32_t>(lastTxData[8])
                  | (static_cast<uint32_t>(lastTxData[9])  << 8)
                  | (static_cast<uint32_t>(lastTxData[10]) << 16)
                  | (static_cast<uint32_t>(lastTxData[11]) << 24);
  EXPECT_EQ(uptime, 120u);

  delete pir;
}
