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
#include <Preferences.h>

// ── Pin definitions ──────────────────────────────────────────────────────────
#define BTN1 16      // Blue   → Volume Up
#define BTN2 17      // Green  → Volume Down
#define BTN3 18      // Yellow → Play/Pause
#define BTN4 19      // Red    → Mute
#define CONFIG_BTN 4  // Config button → Hold 2s to enter config mode

#define OLED_SDA 21
#define OLED_SCL 22
#define OLED_ADDR 0x3C

// ESP32 built-in LED
#define LED_BUILTIN 2

// ── Configuration ───────────────────────────────────────────────────────────
#define DEBOUNCE_MS      50
#define REPEAT_MS        300
#define REPEAT_RATE      80
#define CONFIG_HOLD_MS   2000  // Hold config button for 2s to enter config mode
#define INACTIVITY_MS    10000 // Exit config mode after 10s of inactivity

// ── OLED Display ─────────────────────────────────────────────────────────────
Adafruit_SH1106G display = Adafruit_SH1106G(128, 64, &Wire, -1);


// ── BLE device identity ────────────────────────────────────────────────────────
BleKeyboard bleKeyboard("StreamDeck", "DIY", 100);

// ── NVS Storage ─────────────────────────────────────────────────────────────
Preferences prefs;

// ── Available media key actions ───────────────────────────────────────────────
struct MediaAction {
  const char* name;
  uint8_t key[2];
};

const MediaAction MEDIA_ACTIONS[] = {
  { "VOL+",     {32, 0} },   // KEY_MEDIA_VOLUME_UP
  { "VOL-",     {64, 0} },   // KEY_MEDIA_VOLUME_DOWN
  { "PLAY",     {8, 0} },    // KEY_MEDIA_PLAY_PAUSE
  { "MUTE",     {16, 0} },   // KEY_MEDIA_MUTE
  { "NEXT",     {2, 0} },    // KEY_MEDIA_NEXT_TRACK
  { "PREV",     {4, 0} },    // KEY_MEDIA_PREVIOUS_TRACK
  { "STOP",     {1, 0} },    // KEY_MEDIA_STOP
  { "FWD",      {128, 0} },  // KEY_MEDIA_FAST_FORWARD
  { "RWD",      {0, 1} },    // KEY_MEDIA_REWIND
  { "HOME",     {0, 2} },    // KEY_MEDIA_WWW_HOME
  { "FIND",     {0, 4} },    // KEY_MEDIA_WWW_SEARCH
  { "BMARK",    {0, 8} },    // KEY_MEDIA_WWW_BOOKMARKS
};

const uint8_t MEDIA_ACTION_COUNT = sizeof(MEDIA_ACTIONS) / sizeof(MEDIA_ACTIONS[0]);

// ── Config mode states ───────────────────────────────────────────────────────
enum ConfigState {
  STATE_NORMAL,        // Normal operation
  STATE_SELECT_BUTTON, // Select which button to configure
  STATE_SELECT_ACTION  // Select action to assign
};

ConfigState configState = STATE_NORMAL;
uint8_t selectedButtonIndex = 0;    // Which button we're configuring (0-3)
uint8_t selectedActionIndex = 0;    // Which action we're previewing
uint32_t lastActivityMs = 0;        // For auto-exit config mode
bool configBtnPressed = false;
uint32_t configBtnPressTime = 0;

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
  { BTN1, {32, 0},  "VOL+",     HIGH, false, 0, 0, 0 },
  { BTN2, {64, 0},  "VOL-",     HIGH, false, 0, 0, 0 },
  { BTN3, {8, 0},   "PLAY",     HIGH, false, 0, 0, 0 },
  { BTN4, {16, 0},  "MUTE",     HIGH, false, 0, 0, 0 },
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
void drawConfigMode();
void sendKey(MediaKeyReport key);
void loadButtonConfig();
void saveButtonConfig();
void enterConfigMode();
void exitConfigMode();
void processConfigButton();
void processActionButtons();
void processConfigSelection();
int findActionIndex(const uint8_t* key);
void cycleAction(int direction);
void assignAction(uint8_t buttonIndex, uint8_t actionIndex);

// ── Setup ────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  Serial.println("[Stream Deck] Booting...");

  // Initialize config button
  pinMode(CONFIG_BTN, INPUT_PULLUP);

  // Initialize action buttons
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

  // Load saved button configuration
  loadButtonConfig();

  // Initialize BLE
  bleKeyboard.begin();
  Serial.println("[Stream Deck] BLE advertising — pair from Bluetooth settings.");

  // Initial screen draw
  drawScreen(true);
}

// ── Main loop ─────────────────────────────────────────────────────────────────
void loop() {
  uint32_t now = millis();
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
  if (!nowConnected && configState == STATE_NORMAL) {
    static uint32_t lastBlink = 0;
    if (now - lastBlink > 500) {
      lastBlink = now;
      digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
    }
  }

  // Process config button (long press detection)
  processConfigButton();

  // Process configuration mode
  if (configState != STATE_NORMAL) {
    processConfigSelection();

    // Auto-exit config mode after inactivity
    if (now - lastActivityMs > INACTIVITY_MS) {
      exitConfigMode();
    }
  } else {
    // Normal operation: process action buttons
    processActionButtons();
  }

  // Redraw screen if needed
  if (screenNeedsRedraw) {
    if (configState != STATE_NORMAL) {
      drawConfigMode();
    } else {
      drawScreen(true);
    }
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

  // Color code (2 chars + space)
  const char* colorCode;
  switch(index) {
    case 0: colorCode = "BLU"; break;  // Blue - was BTN1
    case 1: colorCode = "GRN"; break;  // Green - was BTN2
    case 2: colorCode = "YLW"; break;  // Yellow - was BTN3
    case 3: colorCode = "RED"; break;  // Red - was BTN4
    default: colorCode = "???";
  }
  
  display.setCursor(2, y + 2);
  display.print(colorCode);

    // Label (shortened)
    display.setCursor(32, y + 2);
    display.print(buttons[index].label);

  // Reset text color
  display.setTextColor(SH110X_WHITE);
}

// ── Process action buttons (normal mode) ──────────────────────────────────
void processActionButtons() {
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

  if (anyButtonChanged) {
    screenNeedsRedraw = true;
  }
}

// ── Process config button (long press to enter config) ───────────────────
void processConfigButton() {
  static bool lastRaw = HIGH;
  static uint32_t pressStartTime = 0;

  bool raw = digitalRead(CONFIG_BTN);
  uint32_t now = millis();

  if (raw != lastRaw) {
    if (raw == LOW) {
      // Button pressed
      pressStartTime = now;
      configBtnPressed = true;
      configBtnPressTime = now;
    } else {
      // Button released
      configBtnPressed = false;
    }
    lastRaw = raw;
  }

  // Check for long press to enter config mode
  if (configBtnPressed && configState == STATE_NORMAL) {
    if (now - pressStartTime > CONFIG_HOLD_MS) {
      configBtnPressed = false;  // Reset so we don't retrigger
      enterConfigMode();
    }
  }

  // In config mode, config button acts as "back/exit"
  if (configBtnPressed && configState != STATE_NORMAL) {
    if (now - pressStartTime > 500) {  // Short press to exit
      configBtnPressed = false;
      exitConfigMode();
    }
  }
}

// ── Enter configuration mode ────────────────────────────────────────────────
void enterConfigMode() {
  configState = STATE_SELECT_BUTTON;
  selectedButtonIndex = 0;
  lastActivityMs = millis();
  screenNeedsRedraw = true;
  Serial.println("[Stream Deck] Entered CONFIG mode");
}

// ── Exit configuration mode ───────────────────────────────────────────────
void exitConfigMode() {
  saveButtonConfig();
  configState = STATE_NORMAL;
  screenNeedsRedraw = true;
  Serial.println("[Stream Deck] Exited CONFIG mode");
}

// ── Process button selection and action assignment ────────────────────────
void processConfigSelection() {
  uint32_t now = millis();
  bool changed = false;

  for (uint8_t i = 0; i < BTN_COUNT; i++) {
    bool raw = digitalRead(buttons[i].pin);

    // Simple debounce
    static uint32_t lastChangeTimes[4] = {0, 0, 0, 0};
    static bool lastRaws[4] = {HIGH, HIGH, HIGH, HIGH};

    if (raw != lastRaws[i]) {
      lastChangeTimes[i] = now;
      lastRaws[i] = raw;
    }

    if ((now - lastChangeTimes[i]) < DEBOUNCE_MS) continue;

    bool isPressed = (raw == LOW);

    if (isPressed && !buttons[i].pressed) {
      buttons[i].pressed = true;
      lastActivityMs = now;
      changed = true;

      if (configState == STATE_SELECT_BUTTON) {
        // Select this button for configuration
        selectedButtonIndex = i;
        selectedActionIndex = findActionIndex(buttons[i].key);
        configState = STATE_SELECT_ACTION;
      } else if (configState == STATE_SELECT_ACTION) {
        if (i == 0) {
          // BTN1: Previous action
          cycleAction(-1);
        } else if (i == 1) {
          // BTN2: Next action
          cycleAction(1);
        } else if (i == 2) {
          // BTN3: Confirm selection and go back to button selection
          assignAction(selectedButtonIndex, selectedActionIndex);
          configState = STATE_SELECT_BUTTON;
        } else if (i == 3) {
          // BTN4: Cancel and go back
          selectedActionIndex = findActionIndex(buttons[selectedButtonIndex].key);
          configState = STATE_SELECT_BUTTON;
        }
      }
    } else if (!isPressed && buttons[i].pressed) {
      buttons[i].pressed = false;
      changed = true;
    }
  }

  if (changed) {
    screenNeedsRedraw = true;
  }
}

// ── Find action index by key code ───────────────────────────────────────────
int findActionIndex(const uint8_t* key) {
  for (uint8_t i = 0; i < MEDIA_ACTION_COUNT; i++) {
    if (MEDIA_ACTIONS[i].key[0] == key[0] && MEDIA_ACTIONS[i].key[1] == key[1]) {
      return i;
    }
  }
  return 0;
}

// ── Cycle through available actions ────────────────────────────────────────
void cycleAction(int direction) {
  selectedActionIndex += direction;
  if (selectedActionIndex >= MEDIA_ACTION_COUNT) {
    selectedActionIndex = 0;
  } else if (selectedActionIndex < 0) {
    selectedActionIndex = MEDIA_ACTION_COUNT - 1;
  }
}

// ── Assign action to button ────────────────────────────────────────────────
void assignAction(uint8_t buttonIndex, uint8_t actionIndex) {
  buttons[buttonIndex].key[0] = MEDIA_ACTIONS[actionIndex].key[0];
  buttons[buttonIndex].key[1] = MEDIA_ACTIONS[actionIndex].key[1];
  buttons[buttonIndex].label = MEDIA_ACTIONS[actionIndex].name;
  Serial.printf("[Stream Deck] BTN%d assigned to %s\n", buttonIndex + 1, MEDIA_ACTIONS[actionIndex].name);
}

// ── Load button configuration from NVS ──────────────────────────────────────
void loadButtonConfig() {
  prefs.begin("streamdeck", true);  // Read-only

  for (uint8_t i = 0; i < BTN_COUNT; i++) {
    char key[16];
    snprintf(key, sizeof(key), "btn%d_action", i);
    int actionIdx = prefs.getInt(key, i);  // Default to original mapping

    if (actionIdx >= 0 && actionIdx < MEDIA_ACTION_COUNT) {
      assignAction(i, actionIdx);
    }
  }

  prefs.end();
  Serial.println("[Stream Deck] Button config loaded from NVS");
}

// ── Save button configuration to NVS ────────────────────────────────────────
void saveButtonConfig() {
  prefs.begin("streamdeck", false);  // Read-write

  for (uint8_t i = 0; i < BTN_COUNT; i++) {
    char key[16];
    snprintf(key, sizeof(key), "btn%d_action", i);
    int actionIdx = findActionIndex(buttons[i].key);
    prefs.putInt(key, actionIdx);
  }

  prefs.end();
  Serial.println("[Stream Deck] Button config saved to NVS");
}

// ── Draw configuration mode screen ────────────────────────────────────────
void drawConfigMode() {
  display.clearDisplay();

  // Status bar
  display.setCursor(0, 0);
  display.setTextSize(1);
  display.print("SETUP");

  // Horizontal divider
  display.drawLine(0, 10, 127, 10, SH110X_WHITE);

  if (configState == STATE_SELECT_BUTTON) {
    // Show all buttons, highlight selected
    for (uint8_t i = 0; i < BTN_COUNT; i++) {
      int16_t y = 14 + (i * 12);
      
      // Get color code
      const char* colorCode;
      switch(i) {
        case 0: colorCode = "BLU"; break;
        case 1: colorCode = "GRN"; break;
        case 2: colorCode = "YLW"; break;
        case 3: colorCode = "RED"; break;
        default: colorCode = "???";
      }

      if (i == selectedButtonIndex) {
        display.fillRect(0, y - 1, 128, 11, SH110X_WHITE);
        display.setTextColor(SH110X_BLACK);
      } else {
        display.setTextColor(SH110X_WHITE);
      }

      display.setCursor(2, y + 1);
      display.print(colorCode);
      display.print(" ");
      display.print(buttons[i].label);

      display.setTextColor(SH110X_WHITE);
    }

    // Instructions
    display.setCursor(0, 62);
    display.print("SEL=Pick CFG=Exit");

  } else if (configState == STATE_SELECT_ACTION) {
    // Get color code for selected button
    const char* colorCode;
    switch(selectedButtonIndex) {
      case 0: colorCode = "BLU"; break;
      case 1: colorCode = "GRN"; break;
      case 2: colorCode = "YLW"; break;
      case 3: colorCode = "RED"; break;
      default: colorCode = "???";
    }
    
    // Show selected button
    display.setCursor(0, 14);
    display.print(colorCode);
    display.print(" ");
    display.print(buttons[selectedButtonIndex].label);

    // Preview (highlighted)
    display.fillRect(0, 27, 128, 14, SH110X_WHITE);
    display.setTextColor(SH110X_BLACK);
    display.setCursor(4, 30);
    display.print(">");
    display.print(MEDIA_ACTIONS[selectedActionIndex].name);
    display.setTextColor(SH110X_WHITE);

    // Controls
    display.setCursor(0, 46);
    display.print("BLU/GRN: Prev/Next");
    display.setCursor(0, 54);
    display.print("YLW:Save RED:Can CFG:Exit");
  }

  display.display();
}
