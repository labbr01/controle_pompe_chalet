// =============================
// ctl_affiche_chalet_v4.ino
// Afficheur aligné sur ctl_pompe_chalet_v4
//
// Protocole reçu (pompe → afficheur) : SSSAPDDCCPPPPMMMMFGAA (positionnel, 21 chars)
//   SSS  = séquence 000-999
//   A    = statut Air (0=Oui 1=Non 2=Oui* 3=Non*)
//   P    = statut Pompe (4=On 5=Off)
//   DD   = débit 00-10 (2 chars)
//   CC   = courant en dixièmes (00-99 → 0.0-9.9A)
//   PPPP = pression brute (0000-1023)
//   MMMM = minutes pompage consécutives
//   F    = flag purge manuelle (0 ou 1)
//   G    = flag pompe manuelle OFF (0 ou 1)
//   AA   = compteur air brut (00-99)
//
// Commandes envoyées (afficheur → pompe) sur pipe "00002" :
//   CMD1   = toggle purge manuelle (bouton 1)
//   CMD2   = toggle pompe manuelle OFF (bouton 2)
//   (bouton 3 = toggle backlight local, pas de commande RF)
//   RESET! = demande reset (bouton 4, 3s)
//   PING?  = rafraîchissement forcé (automatique tant que pas de données reçues)
//
// Brochage boutons : identique au contrôleur de pompe
//   PIN_BOUTON_1 = A2, PIN_BOUTON_2 = 10, PIN_BOUTON_3 = 3, PIN_BOUTON_RESET = 9
// =============================

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <SPI.h>
#include <nRF24L01.h>
#include <RF24.h>

// LCD 20x4 I2C
LiquidCrystal_I2C lcd(0x27, 20, 4);

// RF24 : CE = 4, CSN = 5
#define CE_PIN  4
#define CSN_PIN 5
RF24 radio(CE_PIN, CSN_PIN);

const byte adresseEtat[6] = "00001"; // Reçoit l'état de la pompe (pipe 0 RX)
const byte adresseCmd[6]  = "00002"; // Envoie les commandes à la pompe (TX)

// Broches boutons (même brochage que le contrôleur de pompe)
const int PIN_BOUTON_1     = A2; // CMD1 - Purge manuelle
const int PIN_BOUTON_2     = 10; // CMD2 - réservé
const int PIN_BOUTON_3     = 3;  // CMD3 - réservé
const int PIN_BOUTON_RESET = 9;  // RESET handshake 3s

// Textes de statut (identique au contrôleur de pompe)
// Indices : 0=Oui, 1=Non, 2=Oui*, 3=Non*, 4=On, 5=Off, 6=Chalet, 7=Purge
const char* statusText[] = {"Oui", "Non", "Oui*", "Non*", "On", "Off", "Chalet", "Purge"};

const int POMPE_MAX_CONSEC_MIN = 6; // Doit correspondre au contrôleur de pompe

// --- État courant reçu de la pompe ---
int  g_seq        = -1;
int  g_idxAir     = 1; // Non
int  g_idxPompe   = 5; // Off
int  g_debit      = 0;
int  g_courantDix = 0; // courant en dixièmes (0-99 → 0.0-9.9A)
int  g_pression   = 0; // pression brute (0-1023)
int  g_minPompe   = 0;
int  g_flagPurge    = 0;
int  g_flagPompeOff = 0; // flag pompe manuelle OFF
int  g_airCount     = 0; // compteur air brut reçu (00-99)
bool g_dataRecu     = false;

// Flag posé par loop() quand RESET_ACK! est reçu, lu par handleResetHandshake()
bool g_resetAckRecu = false;
bool lcdBacklightOn = true; // État du rétroéclairage LCD

// Timing
unsigned long dernierT50ms = 0;
const int INTERVAL_50MS = 50;
unsigned long dernierPing = 0;
const unsigned long PING_INTERVAL_MS = 30000; // 30s - forcer rafraîchissement si pompe ne change pas

// --- Prototypes ---
void sendCmd(const char* cmd);
void updateAffichage();
void handleBoutons();
void handleResetHandshake();
void parseMessage(const char* msg);
void doReset();

// =============================
void setup() {
  //Serial.begin(9600);
  lcd.init();
  
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("AFFICHEUR V4 READY");

  pinMode(PIN_BOUTON_1,     INPUT_PULLUP);
  pinMode(PIN_BOUTON_2,     INPUT_PULLUP);
  pinMode(PIN_BOUTON_3,     INPUT_PULLUP);
  pinMode(PIN_BOUTON_RESET, INPUT_PULLUP);

  if (!radio.begin()) {
    lcd.setCursor(0, 1); lcd.print("RF24 FAIL");
    //Serial.println("[RF24] FAIL");
    while (1);
  }
  radio.setPALevel(RF24_PA_LOW);    // LOW = stable, reset 2-way OK
  //radio.setPALevel(RF24_PA_HIGH);  // HIGH = a tester au chalet si portee insuffisante
  radio.setDataRate(RF24_250KBPS);  // 250KBPS = meilleure sensibilité/portée; 1MBPS si instable
  radio.setChannel(108);            // Canal 108 (hors Wi-Fi 2.4GHz)
  // Modèle deux pipes : TX commandes sur "00002", RX état sur "00001"
  // openReadingPipe(0) APRES openWritingPipe pour que pipe0_reading_address soit correct
  radio.openWritingPipe(adresseCmd);      // TX vers pompe sur 00002
  radio.openReadingPipe(0, adresseEtat);  // RX état depuis pompe sur 00001
  radio.startListening();

  lcd.setCursor(0, 1); lcd.print("RF OK");
  lcd.setCursor(0, 2); lcd.print("En attente pompe...");
  //Serial.println("[V4] Afficheur pret");
}

// =============================
void loop() {
  unsigned long now = millis();

  // Prioritaire : gestion du RESET handshake (bouton 9, 3s)
  handleResetHandshake();

  // Réception des messages d'état de la pompe
  if (radio.available()) {
    char msg[32] = "";
    radio.read(&msg, sizeof(msg));
    //Serial.print("[RF] Recu: ");
    //Serial.println(msg);
    if (strcmp(msg, "RESET_ACK!") == 0) {
      g_resetAckRecu = true; // Sera traité au prochain appel de handleResetHandshake()
    } else if (strcmp(msg, "RESET!") == 0) {
      // La pompe a demandé un reset mutuel (bouton reset sur la boite pompe)
      lcd.setCursor(0, 0); lcd.print("RESET PAR POMPE     ");
      //Serial.println("[RESET] Recu RESET! de la pompe -> reboot");
      delay(200);
      doReset();
    } else {
      parseMessage(msg);
      updateAffichage();
    }
  }

  // Toutes les 50ms : boutons CMD (debounce + détection de front)
  if (now - dernierT50ms >= INTERVAL_50MS) {
    handleBoutons();
    dernierT50ms = now;
  }

  // Toutes les 30s : PING? pour forcer un rafraîchissement de l'affichage
  if (now - dernierPing >= PING_INTERVAL_MS) {
    sendCmd("PING?");
    dernierPing = now;
  }
}

// =============================
// Parse le message compact positionnel SSSAPDCCPPPPMMMMFG (18 chars)
// =============================
void parseMessage(const char* msg) {
  if (strlen(msg) < 21) {
    //Serial.print("[PARSE] Message trop court: "); //Serial.println(msg);
    return;
  }
  char seqStr[4] = {msg[0], msg[1], msg[2], '\0'};
  g_seq       = atoi(seqStr);
  g_idxAir    = msg[3] - '0';
  g_idxPompe  = msg[4] - '0';
  char dStr[3]   = {msg[5], msg[6], '\0'};
  g_debit        = atoi(dStr);
  char ccStr[3]  = {msg[7], msg[8], '\0'};
  g_courantDix   = atoi(ccStr);
  char ppStr[5]  = {msg[9], msg[10], msg[11], msg[12], '\0'};
  g_pression     = atoi(ppStr);
  char minStr[5] = {msg[13], msg[14], msg[15], msg[16], '\0'};
  g_minPompe     = atoi(minStr);
  g_flagPurge    = msg[17] - '0';
  g_flagPompeOff = msg[18] - '0';
  char aaStr[3]  = {msg[19], msg[20], '\0'};
  g_airCount     = atoi(aaStr);
  g_dataRecu     = true;

  // Validation
  if (g_idxAir       < 0 || g_idxAir       > 3)    g_idxAir       = 1;
  if (g_idxPompe     < 4 || g_idxPompe     > 6)    g_idxPompe     = 5; // 6 = arrêt thermique permanent
  if (g_debit        < 0 || g_debit        > 10)   g_debit        = 0;
  if (g_courantDix   < 0 || g_courantDix   > 99)   g_courantDix   = 0;
  if (g_pression     < 0 || g_pression     > 1023) g_pression     = 0;
  if (g_flagPurge    < 0 || g_flagPurge    > 1)    g_flagPurge    = 0;
  if (g_flagPompeOff < 0 || g_flagPompeOff > 1)    g_flagPompeOff = 0;
  if (g_airCount     < 0 || g_airCount     > 99)   g_airCount     = 0;
}

// =============================
// Mise à jour du LCD 20x4 — affichage identique au contrôleur de pompe
// =============================
void updateAffichage() {
  if (!g_dataRecu) return;

  // Arrêt thermique permanent : écran dédié, identique au contrôleur de pompe
  if (g_idxPompe == 6) {
    lcd.clear();
    lcd.setCursor(0,0); lcd.print("SURCHARGE THERMIQUE");
    lcd.setCursor(0,1); lcd.print("Pompe COUPEE");
    lcd.setCursor(0,2); lcd.print("Redemarrage");
    lcd.setCursor(0,3); lcd.print("manuel requis");
    return;
  }

  char buf[21];

  // Ligne 0 : même format que maj_affichage() sur la pompe
  if (digitalRead(PIN_BOUTON_RESET) != LOW) {
    if (g_flagPompeOff) {
      char buf0[21];
      snprintf(buf0, 21, "POMPE MANUEL OFF  %02d", g_airCount);
      lcd.setCursor(0, 0); lcd.print(buf0);
    } else if (g_flagPurge) {
      char buf0[21];
      snprintf(buf0, 21, "PURGE MANUEL ON   %02d", g_airCount);
      lcd.setCursor(0, 0); lcd.print(buf0);
    } else {
      char ligne0[21];
      snprintf(ligne0, 19, "Pompe:%s Air:%s", statusText[g_idxPompe], statusText[g_idxAir]);
      int len0 = strlen(ligne0);
      for (int i = len0; i < 18; i++) ligne0[i] = ' ';
      char acStr[3];
      snprintf(acStr, 3, "%02d", g_airCount);
      ligne0[18] = acStr[0];
      ligne0[19] = acStr[1];
      ligne0[20] = '\0';
      lcd.setCursor(0, 0); lcd.print(ligne0);
    }
  }

  // Ligne 1 : Pompage:Oui/Non + mm'00/MM (pas les secondes, on n'a que les minutes)
  if (g_idxPompe == 4) {
    snprintf(buf, 21, "Pompage:Oui %02d'00/%02d", g_minPompe, POMPE_MAX_CONSEC_MIN);
  } else {
    snprintf(buf, 21, "Pompage:Non 00'00/%02d", POMPE_MAX_CONSEC_MIN);
  }
  buf[20] = '\0';
  lcd.setCursor(0, 1); lcd.print(buf);

  // Ligne 2 : Valve:Chalet/Purge + pression — identique à la pompe
  const char* valve = g_flagPurge ? "Purge " : statusText[6];
  snprintf(buf, 21, "Valve:%-7sP:%4d", valve, g_pression);
  buf[20] = '\0';
  lcd.setCursor(0, 2); lcd.print(buf);

  // Ligne 3 : Débit + courant avec décimale — identique à la pompe
  float courantVal = g_courantDix / 10.0f;
  char courantStr[8];
  snprintf(courantStr, sizeof(courantStr), "I:%1.2fA", courantVal);
  snprintf(buf, 21, "Debit: %2d/10 %-7s", g_debit, courantStr);
  buf[20] = '\0';
  lcd.setCursor(0, 3); lcd.print(buf);
}

// =============================
// Envoi d'une commande à la pompe sur pipe "00002"
// Re-ouvre pipe 0 après TX pour éviter l'écrasement de l'adresse ACK
// =============================
void sendCmd(const char* cmd) {
  radio.stopListening();
  bool ok = radio.write(cmd, strlen(cmd) + 1);
  radio.openReadingPipe(0, adresseEtat); // Restaure le pipe RX après TX
  radio.startListening();
  delay(2);
  if (ok) {
    //Serial.print("[CMD] Envoye: "); //Serial.println(cmd);
  } else {
    //Serial.print("[CMD] Echec: "); //Serial.println(cmd);
  }
}

// =============================
// Gestion boutons CMD (toutes les 50ms, détection de front descendant)
// =============================
void handleBoutons() {
  // Ne pas traiter les CMD si le bouton RESET est tenu (cohérent avec le contrôleur de pompe)
  if (digitalRead(PIN_BOUTON_RESET) == LOW) return;

  static bool lastB1 = false, lastB2 = false, lastB3 = false;
  bool b1 = (digitalRead(PIN_BOUTON_1) == LOW);
  bool b2 = (digitalRead(PIN_BOUTON_2) == LOW);
  bool b3 = (digitalRead(PIN_BOUTON_3) == LOW);

  // Si backlight éteint, n'importe quel bouton l'allume sans exécuter l'action normale
  if (!lcdBacklightOn) {
    if ((b1 && !lastB1) || (b2 && !lastB2) || (b3 && !lastB3)) {
      lcdBacklightOn = true;
      lcd.backlight();
      //Serial.println("[BTN] Backlight rallume");
    }
    lastB1 = b1; lastB2 = b2; lastB3 = b3;
    return;
  }

  if (b1 && !lastB1) { sendCmd("CMD1"); //Serial.println("[BTN1] CMD1 envoye"); 
  }
  if (b2 && !lastB2) { sendCmd("CMD2"); //Serial.println("[BTN2] CMD2 envoye"); 
}
  if (b3 && !lastB3) {
    lcdBacklightOn = !lcdBacklightOn;
    if (lcdBacklightOn) lcd.backlight(); else lcd.noBacklight();
    //Serial.print("[BTN3] Backlight: "); //Serial.println(lcdBacklightOn ? "ON" : "OFF");
  }

  lastB1 = b1;
  lastB2 = b2;
  lastB3 = b3;
}

// =============================
// RESET handshake : bouton 9 tenu 3s → envoie "RESET!" → attend "RESET_ACK!" → doReset()
// =============================
void handleResetHandshake() {
  // Si backlight éteint et bouton reset pressé : allume seulement, bloque jusqu'au relâchement
  static bool resetConsommeBacklight = false;
  if (digitalRead(PIN_BOUTON_RESET) == LOW) {
    if (!lcdBacklightOn && !resetConsommeBacklight) {
      lcdBacklightOn = true; lcd.backlight();
      resetConsommeBacklight = true;
      //Serial.println("[RESET] Backlight rallume, appui consomme");
      return;
    }
    if (resetConsommeBacklight) return; // Bloque jusqu'au relâchement
  } else {
    resetConsommeBacklight = false;
  }

  static bool          countdownActive = false;
  static unsigned long countdownStart  = 0;
  static int           countdownValue  = 3;
  static bool          enAttenteAck    = false;
  static unsigned long ackTimeout      = 0;
  const unsigned long  ACK_TIMEOUT_MS  = 1500;

  // ACK reçu (flag posé par loop())
  if (enAttenteAck && g_resetAckRecu) {
    g_resetAckRecu = false;
    lcd.setCursor(0, 1); lcd.print("ACK RECU - REBOOT   ");
    delay(200);
    doReset();
  }

  // Timeout ACK : on reboot quand même
  if (enAttenteAck && (millis() - ackTimeout > ACK_TIMEOUT_MS)) {
    lcd.setCursor(0, 1); lcd.print("TIMEOUT  - REBOOT   ");
    delay(200);
    doReset();
  }

  if (enAttenteAck) return; // On attend l'ACK, rien d'autre

  // Démarrage du décompte
  if (!countdownActive && digitalRead(PIN_BOUTON_RESET) == LOW) {
    countdownActive = true;
    countdownStart  = millis();
    countdownValue  = 3;
    lcd.setCursor(0, 0); lcd.print("RESET dans 3...     ");
    //Serial.println("[RESET] Decompte 3s...");
  }

  if (countdownActive) {
    if (digitalRead(PIN_BOUTON_RESET) == HIGH) {
      // Bouton relâché avant 3s : annule
      countdownActive = false;
      lcd.setCursor(0, 0); lcd.print("                    ");
      if (g_dataRecu) updateAffichage(); // Restaure ligne 0
      //Serial.println("[RESET] Annule");
    } else {
      unsigned long elapsed = millis() - countdownStart;
      int newVal = 3 - (int)(elapsed / 1000);
      if (newVal > 0 && newVal != countdownValue) {
        countdownValue = newVal;
        char buf[21];
        snprintf(buf, 21, "RESET dans %d...     ", countdownValue);
        lcd.setCursor(0, 0); lcd.print(buf);
        //Serial.print("[RESET] "); //Serial.println(countdownValue);
      }
      if (elapsed >= 3000) {
        countdownActive = false;
        lcd.setCursor(0, 0); lcd.print("RESET EN COURS      ");
        sendCmd("RESET!");
        enAttenteAck = true;
        ackTimeout   = millis();
        //Serial.println("[RESET] RESET! envoye");
      }
    }
  }
}

// =============================
// Reset universel compatible UNO R4 (ARM) et UNO classique (AVR)
// =============================
void doReset() {
#if defined(__arm__) || defined(ARDUINO_ARCH_RENESAS_UNO)
  NVIC_SystemReset();
#else
  asm volatile ("jmp 0");
#endif
}
