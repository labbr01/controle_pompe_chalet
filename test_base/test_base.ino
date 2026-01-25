/*
 * Test MINIMAL - Aucun écran, aucun fil
 * Vérifie que l'Arduino fonctionne
 */

void setup() {
  Serial.begin(9600);
  pinMode(LED_BUILTIN, OUTPUT);
  
  Serial.println("=== ARDUINO FONCTIONNE ===");
  Serial.println("La LED devrait clignoter");
}

void loop() {
  digitalWrite(LED_BUILTIN, HIGH);
  Serial.println("LED ON");
  delay(500);
  
  digitalWrite(LED_BUILTIN, LOW);
  Serial.println("LED OFF");
  delay(500);
}
