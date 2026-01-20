// Programme testrelais pour Arduino
// Fait clignoter la LED du board et active la pin A0 à la même fréquence (10 sec ON, 10 sec OFF)

const int ledPin = LED_BUILTIN; // LED du board
const int relaisPin = A0;       // Pin relais (A0)
const unsigned long interval = 10000; // 10 secondes en millisecondes

void setup() {
  pinMode(ledPin, OUTPUT);
  pinMode(relaisPin, OUTPUT);
}

void loop() {
  digitalWrite(ledPin, HIGH);    // LED ON
  digitalWrite(relaisPin, HIGH); // Relais ON (A0 à HIGH)
  delay(interval);
  digitalWrite(ledPin, LOW);     // LED OFF
  digitalWrite(relaisPin, LOW);  // Relais OFF (A0 à LOW)
  delay(interval);
}
