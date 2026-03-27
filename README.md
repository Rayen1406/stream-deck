# ESP32 Stream Deck

A minimal BLE HID media controller built with an ESP32. Connects wirelessly to your computer as a Bluetooth keyboard/remote — no drivers, no serial scripts, no wires. Features a 1.3" OLED display for status and visual feedback.

**Features:**
- Volume Up/Down
- Play/Pause
- Mute
- OLED display with connection status and button labels
- Visual feedback on button press (inverted row highlighting)

---

## How It Works

The ESP32 acts as a BLE HID device (Bluetooth keyboard). It pairs directly with macOS/Windows/Linux and sends standard media key commands. No host-side software required. The OLED display shows connection status and provides visual feedback when buttons are pressed.

```
[ Button Press ] → ESP32 → BLE HID → OS Media Keys
                    ↓
                 OLED Display (status + visual feedback)
```

---

## Hardware

| Component | Details |
|-----------|---------|
| Microcontroller | ESP32 (30-pin DevKit) |
| Buttons | 4× momentary push buttons |
| Display | 1.3" SH1106 I2C OLED (128×64) |
| Connection | Bluetooth LE |

### Pinout (30-pin ESP32 — right side)

| Component | GPIO Pin | Function |
|-----------|----------|----------|
| BTN1 (Blue) | GPIO 16 | Volume Up (configurable) |
| BTN2 (Green) | GPIO 17 | Volume Down (configurable) |
| BTN3 (Yellow) | GPIO 18 | Play/Pause (configurable) |
| BTN4 (Red) | GPIO 19 | Mute (configurable) |
| **CFG** (White/Black) | **GPIO 23** | **Hold 2s to enter config mode** |
| OLED SDA | GPIO 21 | I2C Data |
| OLED SCL | GPIO 22 | I2C Clock |

### Wiring

**Buttons** (connect between GPIO and GND):
```
GPIO 16 ──[ BTN1 ]── GND   (Volume Up)
GPIO 17 ──[ BTN2 ]── GND   (Volume Down)
GPIO 18 ──[ BTN3 ]── GND   (Play/Pause)
GPIO 19 ──[ BTN4 ]── GND   (Mute)
GPIO 23 ──[ CFG ]─── GND   (Config Button — Hold 2s)
```

**OLED Display** (1.3" SH1106 I2C, address 0x3C):
```
OLED VCC → VIN (5V)
OLED GND → GND
OLED SDA → GPIO 21
OLED SCL → GPIO 22
```

The internal `INPUT_PULLUP` resistors handle button wiring — no external resistors needed.

---

## Configuration Mode

You can remap buttons without recompiling the firmware:

### Enter Config Mode
1. **Hold the CFG button for 2 seconds** (GPIO 23)
2. OLED shows "CONFIG MODE" with button list
3. Auto-exits after 10 seconds of inactivity

### Remap a Button
1. **Press any BTN1-4** to select which button to configure
2. OLED shows current assignment and preview of new action
3. **BTN1**: Previous action | **BTN2**: Next action
4. **BTN3**: Save & return to button list
5. **BTN4**: Cancel & return
6. **Press CFG**: Exit config mode (saves automatically)

### Available Actions
- Volume Up / Down
- Play/Pause / Stop
- Next / Previous Track
- Fast Forward / Rewind
- Mute
- Home / Search / Bookmarks (browser control)

Settings persist in ESP32 flash memory (NVS) and survive reboots.

---

## Firmware

### Requirements

- [Arduino IDE](https://www.arduino.cc/en/software)
- ESP32 board package (version 3.3.6 or later)
- **ESP32 BLE Keyboard** library (custom-patched for NimBLE)
- **NimBLE-Arduino** library (version 2.3.8)
- **Adafruit SH110X** library (for 1.3" OLED)
- **Adafruit SSD1306** library (dependency)
- **Adafruit GFX Library** (dependency)

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

**3. Install OLED libraries (via Arduino Library Manager):**
- **Adafruit SH110X** by Adafruit — for 1.3" SH1106 OLED
- **Adafruit SSD1306** — will be installed automatically as dependency
- **Adafruit GFX Library** — will be installed automatically as dependency

**4. Apply NimBLE 2.x patches** to `BleKeyboard.h` and `BleKeyboard.cpp` (see Troubleshooting section for details).

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
| OLED not working | Check wiring: SDA→GPIO 21, SCL→GPIO 22, VCC→5V, GND→GND |
| OLED shows garbage | Verify I2C address is 0x3C in sketch |
| Display flickering | Check power supply — OLED needs stable 5V |

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
- [x] OLED display with connection status and button labels
- [x] Configurable button mapping (via config button + OLED)
- [ ] Support for long-press and double-press detection
- [ ] Battery level reporting on OLED
- [ ] More buttons / shift layers

---

## License

MIT