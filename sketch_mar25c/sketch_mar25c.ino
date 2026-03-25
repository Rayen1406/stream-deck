#define BTN1 14
#define BTN2 27
#define BTN3 26
#define BTN4 25

void setup() {
  Serial.begin(115200);

  pinMode(BTN1, INPUT_PULLUP);
  pinMode(BTN2, INPUT_PULLUP);
  pinMode(BTN3, INPUT_PULLUP);
  pinMode(BTN4, INPUT_PULLUP);
}

void loop() {
  if (!digitalRead(BTN1)) {
    Serial.println("BLUE");
    delay(300);
  }

  if (!digitalRead(BTN2)) {
    Serial.println("GREEN");
    delay(300);
  }

  if (!digitalRead(BTN3)) {
    Serial.println("YELLOW");
    delay(300);
  }

  if (!digitalRead(BTN4)) {
    Serial.println("RED");
    delay(300);
  }
}