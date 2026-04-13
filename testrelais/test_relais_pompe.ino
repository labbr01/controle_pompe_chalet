// Test simple des relais pompe et purge
// Pompe : D7 (LOW = ON, HIGH = OFF)
// Purge : D8 (LOW = OFF, HIGH = ON)

const int PIN_RELAIS_POMPE = 7;
const int PIN_RELAIS_PURGE = 8;

void setup() {
  pinMode(PIN_RELAIS_POMPE, OUTPUT);
  pinMode(PIN_RELAIS_PURGE, OUTPUT);
  Serial.begin(9600);
  digitalWrite(PIN_RELAIS_POMPE, LOW);  // Pompe ON
  digitalWrite(PIN_RELAIS_PURGE, LOW);  // Purge OFF
  Serial.println("Pompe ON (LOW), Purge OFF (LOW)");
}

void loop() {
  // Pompe toujours ON
  digitalWrite(PIN_RELAIS_POMPE, LOW);
  delay(5000);
  digitalWrite(PIN_RELAIS_POMPE, HIGH);
  delay(5000);

  // // Purge OFF
  // digitalWrite(PIN_RELAIS_PURGE, LOW);
  // Serial.println("Pompe ON (LOW), Purge OFF (LOW)");
  // delay(5000);

  // // Purge ON
  // digitalWrite(PIN_RELAIS_PURGE, HIGH);
  // Serial.println("Pompe ON (LOW), Purge ON (HIGH)");
  // delay(5000);
}
