#ifndef ADAPTER_H
#define ADAPTER_H

#include <Arduino.h>

// Forward declaration to avoid circular include with Mesh
namespace planetopia {
namespace mesh {
struct mesh_message;
}
}

namespace planetopia {
namespace adapter {

// Enum for identifying adapter types
enum adapter_types {
  UNKNOWN_ADAPTER = -1,
  PIR_ADAPTER,
  WIFI_ADAPTER,
  LED_ADAPTER,
  SERIAL_ADAPTER
};

// Abstract base class for all adapters
class Adapter {

protected:
  int _pin;                    // Hardware pin associated with the adapter
  adapter_types _adapterType;  // Type identifier for the adapter
  typedef void (*TransmitPtr)(adapter_types, const uint8_t[12]);
  TransmitPtr mesh_transmit_fn;

public:
  explicit Adapter(int pin);     // Constructor
  virtual ~Adapter() = default;  // Ensure polymorphic destruction

  adapter_types getAdapterType() const;                                        // Returns the adapter type
  void sendDataThroughMesh(const adapter_types type, const uint8_t data[12]);  // sends data through mesh
  void setTransmitFn(TransmitPtr fn);

  virtual bool init() = 0;  // To be implemented by derived classes
  virtual void loop() = 0;  // Called repeatedly in the main loop
  // Called when mesh data is received; filters by adapter type before dispatch
  void onMeshData(const planetopia::mesh::mesh_message& message);

protected:
  // Implement in subclasses: only called when message.dataType == this adapter's type
  virtual void onMeshDataImpl(const planetopia::mesh::mesh_message& message);
};

}
}
#endif
