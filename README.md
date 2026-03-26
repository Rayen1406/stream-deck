# ESP32 Stream Deck

A minimal BLE HID media controller built with an ESP32. Connects wirelessly to your computer as a Bluetooth keyboard/remote — no drivers, no serial scripts, no wires.

**Features:**
- Volume Up/Down
- Play/Pause
- Mute

---

## How It Works

The ESP32 acts as a BLE HID device (Bluetooth keyboard). It pairs directly with macOS/Windows/Linux and sends standard media key commands. No host-side software required.

```
[ Button Press ] → ESP32 → BLE HID → OS Media Keys
```

---

## Hardware

| Component | Details |
|-----------|---------|
| Microcontroller | ESP32 (any variant with BLE) |
| Buttons | 4× momentary push buttons |
| Connection | Bluetooth LE |

### Wiring

| Button | GPIO Pin | Function |
|--------|----------|----------|
| BTN1 (Blue) | GPIO 14 | Volume Up |
| BTN2 (Green) | GPIO 27 | Volume Down |
| BTN3 (Yellow) | GPIO 26 | Play/Pause |
| BTN4 (Red) | GPIO 25 | Mute |

Each button connects between its GPIO pin and **GND**. The internal `INPUT_PULLUP` resistors handle the rest — no external resistors needed.

```
GPIO 14 ──[ BTN1 ]── GND   (Volume Up)
GPIO 27 ──[ BTN2 ]── GND   (Volume Down)
GPIO 26 ──[ BTN3 ]── GND   (Play/Pause)
GPIO 25 ──[ BTN4 ]── GND   (Mute)
```

---

## Firmware

### Requirements

- [Arduino IDE](https://www.arduino.cc/en/software)
- ESP32 board package (version 3.3.6 or later)
- **ESP32 BLE Keyboard** library (custom-patched for NimBLE)
- **NimBLE-Arduino** library (version 2.3.8)

### Library Installation

**1. Install NimBLE-Arduino:**
```bash
cd ~/Documents/Arduino/libraries
git clone --depth 1 https://github.com/h2zero/NimBLE-Arduino.git
```

**2. Install ESP32 BLE Keyboard (patched version):**
```bash
cd ~/Documents/Arduino/libraries
git clone https://github.com/T-vK/ESP32-BLE-Keyboard.git
```

**3. Apply NimBLE 2.x patches** to `BleKeyboard.h` and `BleKeyboard.cpp` (see Troubleshooting section for details).

### Upload

1. Open `sketch_mar25c/sketch_mar25c.ino` in Arduino IDE
2. Select your ESP32 board: **Tools → Board → ESP32 Dev Module**
3. Select the correct port: **Tools → Port** (e.g., `/dev/cu.usbserial-XXXX`)
4. Click **Upload**

### Serial Monitor

Open Serial Monitor (115200 baud) to see connection status:
```
[Stream Deck] Booting...
[Stream Deck] BLE advertising — pair from your Mac's Bluetooth settings.
[Stream Deck] CONNECTED to Mac!
```

---

## Pairing

### macOS Sequoia (and later)

**⚠️ Known Issue:** macOS Sequoia has stricter BLE HID handshake requirements. If you see the device in Bluetooth settings but no "Connect" button appears:

**Solution 1: Reset Bluetooth Cache**
```bash
sudo pkill -9 bluetoothd
# Or reset Bluetooth module via System Settings
```

**Solution 2: Full Bluetooth Reset**
1. Turn off Bluetooth
2. Delete cache files:
   ```bash
   sudo rm /Library/Preferences/com.apple.Bluetooth.plist
   sudo rm ~/Library/Preferences/com.apple.Bluetooth.plist
   ```
3. Restart your Mac
4. Turn Bluetooth back on

**Solution 3: Change Device Name**
If macOS has cached a failed pairing, change the device name in the sketch:
```cpp
BleKeyboard bleKeyboard("StreamDeck", "DIY", 100);  // Try a new name
```

### General Pairing Steps

1. Open **System Settings → Bluetooth**
2. Look for "StreamDeck" in Nearby Devices
3. Click **Connect**
4. The device should appear as "Connected" — no PIN required

### Android / Windows / Linux

Standard BLE pairing — should work without issues. The device appears as a "Bluetooth Keyboard / Media Remote".

---

## Technical Details: NimBLE 2.x Patches

The stock ESP32 BLE Keyboard library needs patches for NimBLE-Arduino 2.3.8 compatibility with macOS Sequoia:

### BleKeyboard.h changes:
- Add `#define USE_NIMBLE` at the top
- Include additional NimBLE headers:
  ```cpp
  #include "NimBLEDevice.h"
  #include "NimBLEServer.h"
  #include "NimBLEAdvertising.h"
  ```
- Update callback signatures for NimBLE 2.x API

### BleKeyboard.cpp changes:
- Use new NimBLE API methods:
  - `getInputReport()` / `getOutputReport()` instead of `inputReport()` / `outputReport()`
  - `setManufacturer()` instead of `manufacturer()->setValue()`
  - `setReportMap()` instead of `reportMap()`
  - `getHidService()` instead of `hidService()`
- Update advertising settings:
  - Remote Control appearance (0x0180) instead of Keyboard
  - Add Battery Service UUID (0x180F)
  - Set advertising interval (20-40ms)
- Enable bonding: `BLEDevice::setSecurityAuth(true, false, false)`
- Update callback signatures with `NimBLEConnInfo&` parameter

---

## Troubleshooting

| Issue | Solution |
|-------|----------|
| No "Connect" button in macOS Bluetooth settings | Reset Bluetooth daemon: `sudo pkill -9 bluetoothd` or restart Mac |
| Device appears but won't pair | Delete Bluetooth cache files and restart |
| Compilation error about NimBLE | Install NimBLE-Arduino: `git clone --depth 1 https://github.com/h2zero/NimBLE-Arduino.git` |
| Works on Android but not macOS | Enable bonding in library, use Remote Control appearance |
| Buttons not responding | Check Serial Monitor for "CONNECTED" message; ensure BLE is paired |

---

## Project Structure

```
stream-deck/
├── sketch_mar25c/
│   └── sketch_mar25c.ino    # Main firmware
├── README.md
└── .git/
```

---

## Roadmap

- [x] BLE HID mode (no host software required)
- [x] macOS Sequoia compatibility
- [ ] Add OLED display to show connection status
- [ ] Support for long-press and double-press detection
- [ ] Configurable button mapping via BLE
- [ ] Battery level reporting
- [ ] More buttons / shift layers

---

## License

MIT