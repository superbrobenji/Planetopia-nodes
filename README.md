# Planetopia - ESP-NOW Mesh Network System

A robust ESP-NOW mesh networking system for ESP32 devices with dynamic adapter capabilities, designed for IoT applications requiring reliable, low-power wireless communication.

## 🚀 Features

- **ESP-NOW Mesh Networking**: Low-latency, peer-to-peer communication between ESP32 devices
- **Dynamic Adapter System**: Runtime switching between different hardware adapters (PIR sensors, LEDs, serial communication, etc.)
- **Master-Node Architecture**: Centralized control through a master node with automatic peer discovery
- **EEPROM Persistence**: Configuration and peer information survives device restarts
- **Health Monitoring**: Real-time status reporting for all nodes in the mesh
- **Serial Communication**: Server integration via serial interface with Protobuf message encoding
- **Remote Configuration**: Change adapter types and settings across the mesh without physical access
- **Automatic Pin Management**: Pin assignments automatically inferred based on adapter type

## 🏗️ Architecture

### Core Components

- **Mesh Network**: Handles peer discovery, routing, and message broadcasting
- **Adapter System**: Abstract interface for different hardware types with factory pattern
- **Serial Interface**: Server communication with structured message handling
- **Configuration Management**: EEPROM-based persistence and remote configuration

### Adapter Types

- **PIR_Adapter**: Motion detection sensor interface
- **Serial_Adapter**: Server communication and mesh control
- **LED_Adapter**: LED control and status indication
- **WIFI_Adapter**: WiFi connectivity management
- **Temp_Adapter**: Temperature sensor interface (example implementation)

## 📋 Requirements

- **Hardware**: ESP32 development board
- **Arduino IDE**: Version 1.8.x or later
- **ESP32 Board Package**: ESP32 Arduino core
- **Dependencies**: ESP-IDF components (WiFi, ESP-NOW, EEPROM)

## 🔧 Setup

### 1. Hardware Configuration

Connect your hardware according to the adapter type:

```cpp
// Default pin assignments (defined in AdapterFactory)
constexpr int PIR_ADAPTER_PIN = 27;
constexpr int LED_ADAPTER_PIN = 26;
constexpr int SERIAL_ADAPTER_PIN = -1;  // No pin needed
constexpr int TEMP_ADAPTER_PIN = 25;
```

### 2. Software Setup

1. **Clone the repository**:
   ```bash
   git clone <repository-url>
   cd planetopia
   ```

2. **Open in Arduino IDE**:
   - Open `main/main.ino` in Arduino IDE
   - Select ESP32 board and correct port
   - Install required libraries if prompted

3. **Configure MAC addresses**:
   - Update `defaultPeerList` in `main.ino` with your device MACs
   - Include the master node's MAC address

4. **Set master node**:
   - Configure one device as master by setting `isMaster` flag
   - Or use the config button (hold for 5 seconds)

### 3. Compilation

```bash
arduino-cli compile --fqbn esp32:esp32:esp32dev main
```

## 🚀 Usage

### Basic Operation

1. **Power on devices**: All nodes automatically join the mesh
2. **Master beaconing**: Master node broadcasts presence every 2 seconds
3. **Peer discovery**: Nodes automatically discover and maintain peer list
4. **Adapter operation**: Each node operates according to its configured adapter

### Serial Communication (Server Integration)

The master node communicates with your server via serial (115200 baud):

- **Message framing**: 2-byte length prefix + Protobuf payload
- **Configuration**: Set adapter types remotely via `OP_CONFIG_SET`
- **Health monitoring**: Request status reports via `OP_HEALTH_REQ`
- **Data forwarding**: Route messages to specific nodes or broadcast to all

### Message Types

- **Targeted messages**: Sent to specific MAC address
- **Broadcast messages**: Sent to all nodes in mesh
- **Control messages**: Configuration and health check operations

## 🔌 Server Integration

For detailed server implementation requirements, see [`server_requirements.md`](server_requirements.md).

### Key Server Responsibilities

- **Serial communication**: Read/write framed messages
- **Protobuf handling**: Parse and construct `MeshMessage` objects
- **Configuration management**: Send adapter type changes
- **Health monitoring**: Request and process status reports
- **Message routing**: Specify target MAC addresses for directed communication

### Go Implementation Example

```go
// Send configuration change
func sendConfigSet(serial *serial.Port, targetMAC [6]byte, adapterType uint8) error {
    msg := &MeshMessage{
        MessageType:        MESH_TYPE_ADAPTER_DATA,
        DataType:          SERIAL_ADAPTER,
        TargetMacAddress:  targetMAC[:],
        Data:              []byte{OP_CONFIG_SET, adapterType},
    }
    return sendFramedMessage(serial, msg)
}
```

## 🛠️ Development

### Adding New Adapters

For detailed development guide, see [`adapter_development_guide.md`](adapter_development_guide.md).

#### Quick Steps:

1. **Create adapter files**:
   - `main/src/Adapter/NewAdapter/NewAdapter.h`
   - `main/src/Adapter/NewAdapter/NewAdapter.cpp`

2. **Update enums**:
   - Add to `adapter_types` in `Adapter.h`
   - Add to `AdapterFactory::createAdapter()`

3. **Define default pin**:
   - Add constant in `AdapterFactory.h`
   - Implement pin logic in `getDefaultPinForAdapter()`

4. **Implement interface**:
   - Inherit from `Adapter` base class
   - Implement `init()`, `loop()`, and `onMeshDataImpl()`

### Changing Default Adapter

```cpp
// Method 1: Code change
void setup() {
    // Change default adapter type
    planetopia::adapter::AdapterFactory::initializeDefaultsIfUnset();
}

// Method 2: Serial command
// Send OP_CONFIG_SET via serial interface

// Method 3: EEPROM clear
// Clear EEPROM and restart device
```

## 📊 Monitoring and Debugging

### Health Reports

Nodes automatically report their status:
- Adapter type
- MAC address
- Uptime
- Connection status

### Logging

Comprehensive logging system with multiple levels:
- **DEBUG**: Detailed operation information
- **INFO**: General status updates
- **WARN**: Non-critical issues
- **ERROR**: Critical problems

### Error Handling

Centralized error management with specific error types:
- `COMMUNICATION_FAIL`: Network or serial issues
- `CONFIG_ERROR`: Configuration problems
- `HARDWARE_ERROR`: Hardware failures

## 🔒 Security

- **Mesh encryption**: 16-byte encryption key stored in EEPROM
- **Peer validation**: MAC address verification for all communications
- **Access control**: Master node controls configuration changes

## 📁 Project Structure

```
planetopia/
├── main/
│   ├── main.ino                 # Main application entry point
│   └── src/
│       ├── Adapter/             # Adapter system
│       │   ├── Adapter.h        # Base adapter class
│       │   ├── AdapterFactory.h # Adapter creation and management
│       │   ├── PIR_Adapter/     # Motion sensor adapter
│       │   ├── Serial_Adapter/  # Server communication adapter
│       │   └── ...
│       ├── Mesh/                # Mesh networking
│       │   ├── Mesh.h          # Mesh protocol and routing
│       │   └── Mesh.cpp        # Mesh implementation
│       ├── hardware/            # Hardware abstractions
│       │   ├── input/          # Input devices (buttons, sensors)
│       │   └── output/         # Output devices (LEDs, displays)
│       └── utils/               # Utility classes
│           ├── Logger.h         # Logging system
│           └── ErrorHandler.h   # Error management
├── server_requirements.md       # Server implementation guide
├── adapter_development_guide.md # Adapter development guide
└── README.md                    # This file
```

## 🤝 Contributing

1. Fork the repository
2. Create a feature branch
3. Implement your changes
4. Add appropriate tests and documentation
5. Submit a pull request

## 📝 License

[Add your license information here]

## 🆘 Support

For issues and questions:
- Check the documentation files
- Review the code examples
- Open an issue on the repository

## 🔄 Version History

- **v1.0.0**: Initial release with ESP-NOW mesh networking
- **v1.1.0**: Added dynamic adapter system and serial communication
- **v1.2.0**: Implemented health monitoring and remote configuration
- **v1.3.0**: Added Protobuf support and simplified server protocol

---

**Note**: This project is designed for ESP32 devices and requires the ESP32 Arduino core. Make sure your development environment is properly configured before attempting to compile or upload the code.
