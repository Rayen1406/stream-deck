/*
 * ESP32 Stream Deck — BLE HID Media Controller
 *
 * Connects to macOS as a Bluetooth keyboard/media remote.
 * No drivers or host scripts needed.
 *
 * Button mapping:
 *   BTN1 (Blue,   GPIO 14) → Volume Up
 *   BTN2 (Green,  GPIO 27) → Volume Down
 *   BTN3 (Yellow, GPIO 26) → Play/Pause
 *   BTN4 (Red,    GPIO 25) → Mute
 *
 * Library required:
 *   "ESP32 BLE Keyboard" by T-vK
 *   Install via Arduino Library Manager or:
 *   https://github.com/T-vK/ESP32-BLE-Keyboard
 */

#include <BleKeyboard.h>

// ── Pin definitions ──────────────────────────────────────────────────────────
#define BTN1 14  // Blue   → Volume Up
#define BTN2 27  // Green  → Volume Down
#define BTN3 26  // Yellow → Play/Pause
#define BTN4 25  // Red    → Mute

// ESP32 built-in LED (GPIO 2 on most dev boards, or change to your board's LED pin)
#define LED_BUILTIN 2

// ── Tuning ───────────────────────────────────────────────────────────────────
#define DEBOUNCE_MS   50    // debounce window in milliseconds
#define REPEAT_MS     300   // delay before hold-repeat kicks in
#define REPEAT_RATE   80    // interval between repeated presses while held (ms)

// ── BLE device identity (shows up on your Mac's Bluetooth menu) ───────────────
BleKeyboard bleKeyboard("StreamDeck", "DIY", 100);

// ── Button state tracker ─────────────────────────────────────────────────────
struct Button {
  uint8_t  pin;
  uint8_t  key[2];              // MediaKeyReport as 2-element array
  bool     lastRaw;             // raw pin reading last loop
  bool     pressed;             // debounced pressed state
  uint32_t lastChangeMs;        // when raw state last changed (for debounce)
  uint32_t heldSinceMs;         // when debounced press started (for repeat)
  uint32_t lastRepeatMs;        // last repeat fire time
};

Button buttons[] = {
  { BTN1, {32, 0},   HIGH, false, 0, 0, 0 },  // KEY_MEDIA_VOLUME_UP
  { BTN2, {64, 0},   HIGH, false, 0, 0, 0 },  // KEY_MEDIA_VOLUME_DOWN
  { BTN3, {8, 0},    HIGH, false, 0, 0, 0 },  // KEY_MEDIA_PLAY_PAUSE
  { BTN4, {16, 0},   HIGH, false, 0, 0, 0 },  // KEY_MEDIA_MUTE
};

const uint8_t BTN_COUNT = sizeof(buttons) / sizeof(buttons[0]);

// ── Helpers ───────────────────────────────────────────────────────────────────
void sendKey(MediaKeyReport key) {
  bleKeyboard.press(key);
  delay(10);
  bleKeyboard.release(key);
}

// ── Setup ─────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  Serial.println("[Stream Deck] Booting...");

  for (uint8_t i = 0; i < BTN_COUNT; i++) {
    pinMode(buttons[i].pin, INPUT_PULLUP);
    buttons[i].lastRaw = HIGH;
  }

  bleKeyboard.begin();
  Serial.println("[Stream Deck] BLE advertising — pair from your Mac's Bluetooth settings.");
}

// ── Main loop ─────────────────────────────────────────────────────────────────
void loop() {
  static bool wasConnected = false;
  bool nowConnected = bleKeyboard.isConnected();
  
  if (nowConnected != wasConnected) {
    wasConnected = nowConnected;
    if (nowConnected) {
      Serial.println("[Stream Deck] CONNECTED to Mac!");
      digitalWrite(LED_BUILTIN, HIGH);
    } else {
      Serial.println("[Stream Deck] DISCONNECTED — restarting advertising");
    }
  }
  
  if (!nowConnected) {
    // Blink the built-in LED while waiting for a connection
    digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
    delay(500);
    return;
  }

  uint32_t now = millis();

  for (uint8_t i = 0; i < BTN_COUNT; i++) {
    Button &btn = buttons[i];
    bool raw = digitalRead(btn.pin);  // LOW = pressed (pull-up)

    // ── Debounce ────────────────────────────────────────────────────────────
    if (raw != btn.lastRaw) {
      btn.lastChangeMs = now;
      btn.lastRaw = raw;
    }

    if ((now - btn.lastChangeMs) < DEBOUNCE_MS) continue;  // still bouncing

    bool isPressed = (raw == LOW);

    // ── Leading edge: first press ────────────────────────────────────────────
    if (isPressed && !btn.pressed) {
      btn.pressed      = true;
      btn.heldSinceMs  = now;
      btn.lastRepeatMs = now;
      sendKey(btn.key);
    }

    // ── Hold: repeat while held ──────────────────────────────────────────────
    if (isPressed && btn.pressed) {
      if ((now - btn.heldSinceMs) > REPEAT_MS &&
          (now - btn.lastRepeatMs) > REPEAT_RATE) {
        sendKey(btn.key);
        btn.lastRepeatMs = now;
      }
    }

    // ── Release ──────────────────────────────────────────────────────────────
    if (!isPressed && btn.pressed) {
      btn.pressed = false;
    }
  }
}
