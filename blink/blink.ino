// Le classique Blink pour Arduino UNO R4 Minima
void setup() {
  pinMode(LED_BUILTIN, OUTPUT); // LED intégrée
}

void loop() {
  digitalWrite(LED_BUILTIN, HIGH); // allume la LED
  delay(2000);                     // attend 2 seconde
  digitalWrite(LED_BUILTIN, LOW);  // éteint la LED
  delay(4000);                     // attend 4 secondes
}