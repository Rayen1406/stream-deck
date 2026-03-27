/*
 * ESP32 Stream Deck — BLE HID Media Controller with OLED
 *
 * Connects to macOS as a Bluetooth keyboard/media remote.
 * Features 1.3" SH1106 OLED display for status and visual feedback.
 *
 * Pinout (30-pin ESP32, right side):
 *   BTN1 (Blue,   GPIO 16) → Volume Up
 *   BTN2 (Green,  GPIO 17) → Volume Down
 *   BTN3 (Yellow, GPIO 18) → Play/Pause
 *   BTN4 (Red,    GPIO 19) → Mute
 *   OLED SDA → GPIO 21
 *   OLED SCL → GPIO 22
 *   OLED VCC → VIN (5V)
 *   OLED GND → GND
 *
 * Libraries required:
 *   - ESP32 BLE Keyboard by T-vK (NimBLE-patched)
 *   - Adafruit SH110X by Adafruit
 *   - Adafruit SSD1306 (dependency)
 *   - Adafruit GFX Library (dependency)
 */

#include <BleKeyboard.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>

// ── Pin definitions ──────────────────────────────────────────────────────────
#define BTN1 16  // Blue   → Volume Up
#define BTN2 17  // Green  → Volume Down
#define BTN3 18  // Yellow → Play/Pause
#define BTN4 19  // Red    → Mute

#define OLED_SDA 21
#define OLED_SCL 22
#define OLED_ADDR 0x3C

// ESP32 built-in LED
#define LED_BUILTIN 2

// ── OLED Display ─────────────────────────────────────────────────────────────
Adafruit_SH1106G display = Adafruit_SH1106G(128, 64, &Wire, -1);

// ── Tuning ───────────────────────────────────────────────────────────────────
#define DEBOUNCE_MS   50
#define REPEAT_MS     300
#define REPEAT_RATE   80

// ── BLE device identity ────────────────────────────────────────────────────────
BleKeyboard bleKeyboard("StreamDeck", "DIY", 100);

// ── Button configuration ─────────────────────────────────────────────────────
struct Button {
  uint8_t  pin;
  uint8_t  key[2];
  const char* label;
  bool     lastRaw;
  bool     pressed;
  uint32_t lastChangeMs;
  uint32_t heldSinceMs;
  uint32_t lastRepeatMs;
};

Button buttons[] = {
  { BTN1, {32, 0},  "Vol Up",     HIGH, false, 0, 0, 0 },
  { BTN2, {64, 0},  "Vol Down",   HIGH, false, 0, 0, 0 },
  { BTN3, {8, 0},   "Play/Pause", HIGH, false, 0, 0, 0 },
  { BTN4, {16, 0},  "Mute",       HIGH, false, 0, 0, 0 },
};

const uint8_t BTN_COUNT = sizeof(buttons) / sizeof(buttons[0]);

// ── Display state tracking ───────────────────────────────────────────────────
bool lastConnState = false;
bool lastPressedStates[4] = {false, false, false, false};
bool screenNeedsRedraw = true;

// ── Forward declarations ─────────────────────────────────────────────────────
void drawScreen(bool force = false);
void drawStatusBar(bool connected);
void drawButtonRow(uint8_t index, bool pressed, bool force = false);
void sendKey(MediaKeyReport key);

// ── Setup ────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  Serial.println("[Stream Deck] Booting...");

  // Initialize buttons
  for (uint8_t i = 0; i < BTN_COUNT; i++) {
    pinMode(buttons[i].pin, INPUT_PULLUP);
    buttons[i].lastRaw = HIGH;
  }

  // Initialize LED
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

  // Initialize OLED
  Wire.begin(OLED_SDA, OLED_SCL);
  delay(250);
  if (!display.begin(OLED_ADDR, true)) {
    Serial.println("[Stream Deck] OLED initialization failed!");
  } else {
    Serial.println("[Stream Deck] OLED initialized.");
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SH110X_WHITE);
    display.display();
  }

  // Initialize BLE
  bleKeyboard.begin();
  Serial.println("[Stream Deck] BLE advertising — pair from Bluetooth settings.");

  // Initial screen draw
  drawScreen(true);
}

// ── Main loop ─────────────────────────────────────────────────────────────────
void loop() {
  bool nowConnected = bleKeyboard.isConnected();

  // Check connection state change
  if (nowConnected != lastConnState) {
    lastConnState = nowConnected;
    screenNeedsRedraw = true;
    if (nowConnected) {
      Serial.println("[Stream Deck] CONNECTED!");
      digitalWrite(LED_BUILTIN, HIGH);
    } else {
      Serial.println("[Stream Deck] DISCONNECTED");
      digitalWrite(LED_BUILTIN, LOW);
    }
  }

  // Handle disconnected state (blink LED)
  if (!nowConnected) {
    static uint32_t lastBlink = 0;
    if (millis() - lastBlink > 500) {
      lastBlink = millis();
      digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
    }
  }

  // Process buttons
  uint32_t now = millis();
  bool anyButtonChanged = false;

  for (uint8_t i = 0; i < BTN_COUNT; i++) {
    Button &btn = buttons[i];
    bool raw = digitalRead(btn.pin);

    // Debounce
    if (raw != btn.lastRaw) {
      btn.lastChangeMs = now;
      btn.lastRaw = raw;
    }

    if ((now - btn.lastChangeMs) < DEBOUNCE_MS) continue;

    bool isPressed = (raw == LOW);

    // Press state changed?
    if (isPressed != btn.pressed) {
      btn.pressed = isPressed;
      anyButtonChanged = true;

      if (isPressed) {
        btn.heldSinceMs = now;
        btn.lastRepeatMs = now;
        sendKey(btn.key);
      }
    }

    // Hold repeat
    if (isPressed && btn.pressed) {
      if ((now - btn.heldSinceMs) > REPEAT_MS &&
          (now - btn.lastRepeatMs) > REPEAT_RATE) {
        sendKey(btn.key);
        btn.lastRepeatMs = now;
      }
    }
  }

  // Redraw screen if needed
  if (screenNeedsRedraw || anyButtonChanged) {
    drawScreen(screenNeedsRedraw);
    screenNeedsRedraw = false;
  }
}

// ── Send media key ───────────────────────────────────────────────────────────
void sendKey(MediaKeyReport key) {
  if (bleKeyboard.isConnected()) {
    bleKeyboard.press(key);
    delay(10);
    bleKeyboard.release(key);
  }
}

// ── Draw complete screen ─────────────────────────────────────────────────────
void drawScreen(bool force) {
  bool connected = bleKeyboard.isConnected();

  // Check what needs updating
  bool statusChanged = (connected != lastConnState) || force;
  bool anyButtonChanged = force;

  for (uint8_t i = 0; i < BTN_COUNT; i++) {
    if (buttons[i].pressed != lastPressedStates[i]) {
      lastPressedStates[i] = buttons[i].pressed;
      anyButtonChanged = true;
    }
  }

  if (!statusChanged && !anyButtonChanged && !force) return;

  // Full redraw only on force/connection change, otherwise partial
  if (force || statusChanged) {
    display.clearDisplay();
    drawStatusBar(connected);

    // Horizontal divider
    display.drawLine(0, 10, 127, 10, SH110X_WHITE);

    // Draw all button rows
    for (uint8_t i = 0; i < BTN_COUNT; i++) {
      drawButtonRow(i, buttons[i].pressed, true);
    }
  } else if (anyButtonChanged) {
    // Only redraw changed button rows
    for (uint8_t i = 0; i < BTN_COUNT; i++) {
      drawButtonRow(i, buttons[i].pressed, false);
    }
  }

  display.display();
}

// ── Draw status bar ─────────────────────────────────────────────────────────
void drawStatusBar(bool connected) {
  // Left: "StreamDeck"
  display.setCursor(0, 0);
  display.setTextSize(1);
  display.print("StreamDeck");

  // Right: connection status
  display.setCursor(80, 0);
  if (connected) {
    display.print("BLE OK");
  } else {
    display.print("pairing..");
  }
}

// ── Draw button row ───────────────────────────────────────────────────────────
void drawButtonRow(uint8_t index, bool pressed, bool force) {
  if (!force && (pressed == lastPressedStates[index])) return;

  int16_t y = 14 + (index * 12);  // 12 pixels per row, starting below divider

  // Clear row area
  display.fillRect(0, y, 128, 12, SH110X_BLACK);

  if (pressed) {
    // Inverted: white background, black text
    display.fillRect(0, y, 128, 12, SH110X_WHITE);
    display.setTextColor(SH110X_BLACK);
  } else {
    // Normal: black background, white text
    display.setTextColor(SH110X_WHITE);
  }

  // Button number
  display.setCursor(4, y + 2);
  display.print("BTN");
  display.print(index + 1);

  // Label
  display.setCursor(40, y + 2);
  display.print(buttons[index].label);

  // Reset text color
  display.setTextColor(SH110X_WHITE);
}
