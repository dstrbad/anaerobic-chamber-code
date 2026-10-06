#define RELAY_PIN 23

void setup() {
  pinMode(RELAY_PIN, OUTPUT);

  digitalWrite(RELAY_PIN, LOW);   // ON (adjust if needed)
  delay(5000);
  digitalWrite(RELAY_PIN, HIGH);  // OFF
  
}

void loop() {
  // do nothing
}