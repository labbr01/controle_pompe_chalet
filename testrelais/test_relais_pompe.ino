// Test simple du relais de pompe
// Relais NC : LOW = pompe ON, HIGH = pompe OFF

const int PIN_RELAIS_POMPE = 7;

void setup() {
  pinMode(PIN_RELAIS_POMPE, OUTPUT);
  Serial.begin(9600);
  digitalWrite(PIN_RELAIS_POMPE, LOW); // Pompe ON au démarrage
  Serial.println("Pompe ON (LOW)");
}

void loop() {
  delay(5000); // 5 secondes ON
  digitalWrite(PIN_RELAIS_POMPE, HIGH); // Pompe OFF
  Serial.println("Pompe OFF (HIGH)");
  delay(5000); // 5 secondes OFF
  digitalWrite(PIN_RELAIS_POMPE, LOW); // Pompe ON
  Serial.println("Pompe ON (LOW)");
}
