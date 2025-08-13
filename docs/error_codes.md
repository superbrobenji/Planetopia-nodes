# Error Code Reference (Seven-Segment Display)

The display always shows a 4-digit number **T M SS** where:

| Position | Name       | Description                                     |
|----------|------------|-------------------------------------------------|
| `T`      | Type       | 0=OK, 1=Hardware, 2=Comm, 3=Memory, 4=Config, 5=Logic |
| `M`      | Module     | 0=Core, 1=Adapter, 2=Mesh, 3=Persistence, 4=Network   |
| `SS`     | Subsystem  | 00-99 specific to the module                      |

---
## 1 × ××  Hardware errors
| Code | Location                | Meaning                                   |
|------|-------------------------|-------------------------------------------|
| 1101 | Core                    | Brown-out or unexpected reset             |
| 1201 | Adapter – PIR           | PIR sensor failed to init                |
| 1202 | Adapter – Serial        | Serial adapter buffer overflow           |
| 1301 | Persistence             | EEPROM failed to begin                   |

## 2 × ××  Communication errors
… *(extend as you add codes)* …

## How to add a new code
```cpp
#include "src/error/Error.h"

// Example: mesh peer-list overflow in the Mesh module (sub-id 0x01)
constexpr uint8_t MESH_PEER_OVERFLOW = 0x01; // local registry only – for the table below

// Report the error in one line:
planetopia::err::fail(utils::ErrorType::MEMORY_ERROR,
                      "Peer vector overflow");
/*
 * The helper does three things for you:
 *  1) Logs the message at ERROR level
 *  2) For boards with a seven-segment display it shows 3-2-01
 *       (MEMORY / MESH / 01) automatically
 *  3) Blinks the error LED four times (MEMORY category) and, if configured,
 *     triggers a safe reboot.
 *
 * Note: never call ErrorCore directly – always go through the helpers in
 *       src/error/Error.h (err::fail, err::fatal, ERROR_CHECK*, …).
 */
```