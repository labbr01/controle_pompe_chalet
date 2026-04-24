// List TODO for check
// lastPompeOn supprimé
// etatAirPrecedentOui sert dans evaluerStatutAir de V2, pas encore reporté ici, mais initialisé et la valeur change jamais, faut voir
// lastSample contenait le now = millis() dans la loop  probablement remplacé par les 3 variables de type dernierTraitement.50ms  Supprimé!
// lastAffichage sert dans le traitement d el'affichage (à implanter)
// INTERVAL_SAMPLE et INTERVAL_AFFICHAGE sont remplacés par  les constentes de type const int 50ms=50;
// etatPompagePrecedent assigné jamais lu supprimé!
// unsigned long lastPompageStateChange = 0; jamais utilisé supprimé
// minutesPauseRestantes  assigné mais jamais utilisé SAUF QUE EN THEORIE un afficheur deveit indiquer le temps qu'il reste a la pause thermique.  FAUT UN DOUBLE CHECK
// lastDebitState  est utilisé, il contient la lecture précédente de la lecture courante pour vérifier s'il y a un changement
// pulseCount  un compteur qu'on incrément mais qu'on utilise pas ca sert a quoi.  En plus lié a lastDebitState
// debitImpulsions est lié a pulseCount, et il fini par servir a pompeVraimentActive qui lui est utilisé
// sondeIRMoy, courantMin, courantMax, debitMin, debitMax (utilisées dans calculer_stats, mais leur usage dans la logique métier/affichage est à vérifier) -> EFFACÉ
// dernierStatutAir  supprimé
// tDebutRetourNormal  supprimé
// lastEncodedMsg  supprimé
// repeatCount semble utilisé pour éviter de répéter plus que 5 fois le meme message
// pompeRedemarrage  ne sert a rien, il est assigné a faux on check s'il est vrai pour faire quelque chose! supprimé!
// tDebutSecurite  DOUTEUX, j'aimerais qu'on réalnalyse.  C'est quoi le statut en attente redémarrage, pourquoi c'est la
// Faudrait assi m'expliquer tDebutAnomalie 
//  Aussi besoin d'explication pour tDernierEtatAir et toutes les autres variables apres.  Ca semble compliqué pas mal.  tout ce qu'on veut c'est gérer l'affichage au seconde et ne pas recommuniquer le même affichage au ctl_affiche_chalet.




#include <Arduino.h>
#include <stdint.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <SPI.h>
#include <nRF24L01.h>
#include <RF24.h>

// === CONSTANTES IMMUABLES (hardware, textes, indices) ===
// Broches matérielles
const int PIN_RELAIS_POMPE = 7;   // D7
const int PIN_RELAIS_PURGE = 8;   // D8
const int PIN_BOUTON_RESET = 9;   // D9 (à adapter selon dispo)
const int PIN_BOUTON_1 = A2;
const int PIN_BOUTON_2 = 10;
const int PIN_BOUTON_3 = 3;

// Tableau unique pour tous les statuts (pour encodage et affichage)
// Indices : 0=Oui, 1=Non, 2=Oui*, 3=Non*, 4=On, 5=Off, 6=Chalet, 7=Purge
const char* statusText[] = {"Oui", "Non", "Oui*", "Non*", "On", "Off", "Chalet", "Purge"};

// === CONSTANTES AJUSTABLES (délais, temps, protections) ===
const int POMPE_MAX_CONSEC_MIN = 6;      // 2 min ON consécutives max (test)
const int POMPE_PAUSE_MIN = 3;           // Pause forcée 3 min (test)
// Pour archivage : remettre 20/60 min

// Délais (en ms) pour la gestion d'anomalie d'air
const unsigned long DELAI_AIR_OUI = 15000;      // 15s (au lieu de 30s)
const unsigned long DELAI_AIR_OUIE = 45000;     // 45s (au lieu de 90s)
const unsigned long DELAI_AIR_NON = 15000;      // 15s (au lieu de 30s)
const unsigned long DELAI_AIR_NONE = 45000;     // 45s (au lieu de 90s)
const unsigned long DELAI_ANOMALIE_MAX = 60000; // 60s (1min)

// Durée NON consécutif pour reset du compteur pompage
const unsigned long DUREE_NON_POMPAGE_RESET_MS = 5000; // 5 secondes

// --- structure pour la valeur des capteurs du Arduino ---
struct Capteurs {
  bool sondeIR;
  bool Debit;
  int16_t Pression;   // ou uint16_t si jamais négatif impossible
  int16_t Courant;
  bool Bouton1;
  bool Bouton2;
  bool Bouton3;
  bool BoutonReset;
  bool RelaisPompe;
  bool RelaisPurge;
};

// Variables globales pour les capteurs et leur dernier traitement  
Capteurs capteurs = {false, false, 0, 0, false, false, false, false, false, false};

// Structure pour les valeurs de derniers traitements (pour gestion des timings)
struct DernierTraitement {
  long t50ms;
  long t100ms;
  long t1000ms;
};

// Dernieres valeurs de milis() pour chaque type de tâche
DernierTraitement dernierTraitement = {0, 0, 0};

const int INTERVAL_50MS = 50;   // ms
const int INTERVAL_100MS = 100;   // ms
const int INTERVAL_1000MS = 1000;   // ms

// --- Variables d'état pour l'anomalie d'air ---
enum EtatAnomalieAir { NORMAL, ANOMALIE, SECURITE, ATTENTE_REDEMARRAGE };
EtatAnomalieAir etatAnomalieAir = NORMAL;
unsigned long tDebutAnomalie = 0;
unsigned long tDebutSecurite = 0;
unsigned long tDernierEtatAir = 0;

float debitMoy = 0; // Moyenne du débit sur NBUF échantillons
float courantMoy = 0;

// --- Prototypes ---

void gestionAnomalieAir(int statutAir);
void resetBuffers();
void calculer_stats();
void maj_affichage(int statutAirCourant);
void communiquer_chalet();
int evaluerStatutAir(bool pompeEnMarche);
bool sendRFMessage(const char* msg);
/*
 * ============================================================================
 * Contrôleur Pompe Chalet - Structure modulaire, acquisition rapide, affichage capteurs
 * ============================================================================
 *
 * Cette version lit les capteurs (sonde IR, débitmètre, courant) toutes les 100ms,
 * calcule la moyenne/min/max sur 1s, puis affiche les valeurs sur le LCD 20x4.
 * Les fonctions de relais et communication sont présentes mais inactives.
 *
 *
 * Brochage Arduino (sorties) :
 * - Sonde IR (présence air/eau) : D2
 * - Débitmètre : D6
 * - Capteur de courant (ACS712) : A1
 * - Relais Pompe : D7 (prévu)
 * - Relais Purge : D8 (prévu)
 *
 * LCD I2C 20x4 :
 * - SDA → A4 (I2C)
 * - SCL → A5 (I2C)
 * - VCC → 5V
 * - GND → GND
 *
 * ============================================================================
 */

// Déclaration de l'objet radio RF24 (filage identique à test_comm_maitre : CE = 4, CSN = 5)
#define CE_PIN 4
#define CSN_PIN 5
RF24 radio(CE_PIN, CSN_PIN);

LiquidCrystal_I2C lcd(0x27, 20, 4);

// Broches capteurs (adapter si besoin)
const int PIN_SONDE_IR = 2;   // D2
const int PIN_DEBIT = 6;      // D6
const int PIN_COURANT = A1;   // A1
const int PIN_PRESSION = A0;  // A0 (capteur de pression)

// Buffers pour 1 seconde (10 échantillons)
const int NBUF = 10;
bool bufDebit[NBUF];
float bufCourant[NBUF];
bool bufSondeIR[NBUF];
int idxBuf = 0;

// --- Affichage simple/détaillé du débit ---
bool afficherDebitSimple = true; // Mettre à false pour l'ancien affichage


// Buffer circulaire pour le statut Air (10s d'historique)
const int NBUF_AIR = 100;
int bufAir[NBUF_AIR];
int idxBufAir = 0;
bool etatAirPrecedentOui = false;
bool purgeManuelleActive = false; // Purge manuelle activée par bouton 1
bool pompeManuelleOff = false;   // Pompe coupée manuellement par bouton 2
bool lcdBacklightOn = true;      // État du rétroéclairage LCD

// Timing
unsigned long lastAffichage = 0;

volatile unsigned int pulseCount = 0;
int debitImpulsions = 0; // Nombre d'impulsions par seconde
int lastDebitState = 0;

void handleCaptureSondes() {
    capteurs.sondeIR = digitalRead(PIN_SONDE_IR);
    capteurs.Debit = digitalRead(PIN_DEBIT);    
    capteurs.Courant = analogRead(PIN_COURANT);
    capteurs.Pression = analogRead(PIN_PRESSION);
    capteurs.Bouton1 = digitalRead(PIN_BOUTON_1);
    capteurs.Bouton2 = digitalRead(PIN_BOUTON_2);
    capteurs.Bouton3 = digitalRead(PIN_BOUTON_3);
    capteurs.BoutonReset = digitalRead(PIN_BOUTON_RESET);
    capteurs.RelaisPompe = digitalRead(PIN_RELAIS_POMPE);
    capteurs.RelaisPurge = digitalRead(PIN_RELAIS_PURGE);   
}

void setup() {
  pinMode(PIN_RELAIS_POMPE, OUTPUT);
  pinMode(PIN_RELAIS_PURGE, OUTPUT);
  pinMode(PIN_BOUTON_RESET, INPUT_PULLUP);
  pinMode(PIN_BOUTON_1, INPUT_PULLUP);
  pinMode(PIN_BOUTON_2, INPUT_PULLUP);
  pinMode(PIN_BOUTON_3, INPUT_PULLUP);
  digitalWrite(PIN_RELAIS_POMPE, LOW); // Pompe ON au démarrage
  digitalWrite(PIN_RELAIS_PURGE, LOW); // Purge OFF au démarrage
  //Serial.begin(9600);
  lcd.init();
  lcd.backlight();
  delay(100);

  // Initialisation du module radio RF24
  if (!radio.begin()) {
    //Serial.println("[RF24] Erreur d'initialisation du module radio!");
  } else {
    radio.setPALevel(RF24_PA_LOW); // Puissance basse pour tests
    radio.setDataRate(RF24_1MBPS); // Débit standard
    const byte adresse[6] = "00001"; // État pompe → afficheur
    radio.openReadingPipe(0, adresse);
    const byte adresseCmd[6] = "00002"; // Commandes afficheur → pompe
    radio.openReadingPipe(1, adresseCmd);
    radio.openWritingPipe(adresse);
    radio.startListening();
    lcd.setCursor(0,1);
    lcd.print("RF OK");
    //Serial.println("RF OK [RF24] Module radio initialisé");
  }

  pinMode(PIN_SONDE_IR, INPUT);
  pinMode(PIN_DEBIT, INPUT);
  // PIN_COURANT = A1 (analogique)
  // lastPeriodicState = millis(); // supprimé, variable non déclarée
  handleCaptureSondes(); // Capture initiale pour éviter les valeurs indéterminées
  lastDebitState = capteurs.Debit;
}

// --- Variables pour la protection thermique ---
unsigned int minutesPompageConsecutives = 0;
bool pauseThermiqueActive = false;
unsigned long debutPauseThermique = 0;
unsigned int minutesPauseRestantes = 0;

// Début du cycle de pompage continu (pour affichage mm'ss)
unsigned long debutPompage = 0;
bool forceSendEtat = false; // Posé à true par PING? pour forcer l'envoi même si rien n'a changé


void handleBouton1() {
  static bool lastPressed = false;
  bool pressed = (digitalRead(PIN_BOUTON_1) == LOW);
  if (pressed && !lastPressed) {
    if (!lcdBacklightOn) {
      lcdBacklightOn = true; lcd.backlight(); //Serial.println("[BTN1] Backlight rallume");
    } else {
      purgeManuelleActive = !purgeManuelleActive;
      digitalWrite(PIN_RELAIS_PURGE, purgeManuelleActive ? HIGH : LOW);
    }
  }
  lastPressed = pressed;
}
void handleBouton2() {
  static bool lastPressed = false;
  bool pressed = (digitalRead(PIN_BOUTON_2) == LOW);
  if (pressed && !lastPressed) {
    if (!lcdBacklightOn) {
      lcdBacklightOn = true; lcd.backlight(); //Serial.println("[BTN2] Backlight rallume");
    } else {
      pompeManuelleOff = !pompeManuelleOff;
      digitalWrite(PIN_RELAIS_POMPE, pompeManuelleOff ? HIGH : LOW);
      //Serial.print("[BTN2] Pompe manuelle OFF: "); //Serial.println(pompeManuelleOff ? "ON" : "OFF");
    }
  }
  lastPressed = pressed;
}
void handleBouton3() {
  static bool lastPressed = false;
  bool pressed = (digitalRead(PIN_BOUTON_3) == LOW);
  if (pressed && !lastPressed) {
    lcdBacklightOn = !lcdBacklightOn;
    if (lcdBacklightOn) lcd.backlight(); else lcd.noBacklight();
    //Serial.print("[BTN3] Backlight: "); //Serial.println(lcdBacklightOn ? "ON" : "OFF");
  }
  lastPressed = pressed;
}
bool handleBoutonReset() {
  static unsigned long boutonResetStart = 0;
  static int lastCountdown = -1;
  bool appuye = (digitalRead(PIN_BOUTON_RESET) == LOW); // INPUT_PULLUP, lecture directe
  static bool resetConsommeBacklight = false;
  if (appuye && !lcdBacklightOn && !resetConsommeBacklight) {
    lcdBacklightOn = true; lcd.backlight();
    resetConsommeBacklight = true;
    //Serial.println("[RESET] Backlight rallume, appui consomme");
    return true; // Bloque boutons pendant l'appui mais sans décompte
  }
  if (appuye && resetConsommeBacklight) return true; // Maintient le blocage jusqu'au relâchement
  if (!appuye) resetConsommeBacklight = false;
  if (appuye) {
    if (boutonResetStart == 0) boutonResetStart = millis();
    unsigned long elapsed = millis() - boutonResetStart;
    int countdown = 3 - (int)(elapsed / 1000);
    if (countdown < 1) countdown = 1;
    if (countdown != lastCountdown) {
      lastCountdown = countdown;
      char ligne[21];
      snprintf(ligne, sizeof(ligne), "RESET dans %d...    ", countdown);
      lcd.setCursor(0, 0);
      lcd.print(ligne);
      //Serial.print("[RESET] Compte a rebours: ");
      //Serial.println(countdown);
    }
    if (elapsed >= 3000) {
      //Serial.println("[RESET] 3s appui -> envoie RESET! -> NVIC_SystemReset");
      sendRFMessage("RESET!"); // Notifie l'afficheur avant de redémarrer
      delay(100);
      NVIC_SystemReset();
    }
    return true; // Bloque les autres boutons pendant l'appui
  } else {
    if (boutonResetStart != 0) {
      // Bouton relâché avant 3s : efface le message
      lcd.setCursor(0, 0);
      lcd.print("                    ");
      lastAffichage = 0; // Force rafraîchissement LCD immédiat
    }
    boutonResetStart = 0;
    lastCountdown = -1;
    return false;
  }
}

void handleSondeIr() {
  int etat = capteurs.sondeIR;
  bufSondeIR[idxBuf] = etat;
  // Met à jour le buffer Air (0 = Non, 1 = Oui)
  bufAir[idxBufAir] = (etat < 1) ? 1 : 0; // IR bas = Air
  idxBufAir = (idxBufAir + 1) % NBUF_AIR;
}

void handledebimetre() {
    // Logique pour le débitmètre
    bufDebit[idxBuf] = capteurs.Debit;
    if (lastDebitState == 0 && capteurs.Debit == 1) {
        pulseCount++;
    }
    lastDebitState = capteurs.Debit;
}   

void handleCourant() {
  // Logique pour le capteur de courant
  int raw = capteurs.Courant;
  float tension = raw * 5.0 / 1023.0;
  float offset = 2.5; // V (pour ACS712)
  float sensibilite = 0.185; // V/A (pour ACS712-5A)
  float courant = (tension - offset) / sensibilite;
  bufCourant[idxBuf] = courant;
}

// aucune logique de pression pour l'instant, juste lecture et stockage 
// void handelePression() {
//   // Logique pour le capteur de pression  
// }

void handleAffichage(unsigned long now) {
    // Logique pour l'affichage sur le LCD    lastAffichage = now;
    // Si la pompe est coupée (SECURITE ou ATTENTE_REDEMARRAGE), on bloque tout affichage autre que le message d'erreur
    if (etatAnomalieAir == SECURITE || etatAnomalieAir == ATTENTE_REDEMARRAGE) {
        // Plus de reset logiciel automatique ici. Seul le bouton humain dans loop() fait un reset.
        lcd.clear();
        lcd.setCursor(0,0); lcd.print("ANOMALIE AIR");
        lcd.setCursor(0,1); lcd.print("Pompe COUPEE");
        lcd.setCursor(0,2); lcd.print("Redemarrage");
        lcd.setCursor(0,3); lcd.print("manuel requis");
        return;
    }

    // Si pause thermique active, on n'interprète rien, on affiche juste l'état de pause
    static bool pauseThermiqueLogEntree = false;
    if (pauseThermiqueActive) {
      unsigned long tempsEcoule = (now - debutPauseThermique) / 1000UL;
      unsigned int minutesEcoulees = tempsEcoule / 60;
      minutesPauseRestantes = (POMPE_PAUSE_MIN > minutesEcoulees) ? (POMPE_PAUSE_MIN - minutesEcoulees) : 0;
      if (!pauseThermiqueLogEntree) {
        //Serial.print("[THERMIQUE] Début pause thermique à t=");
        //Serial.print(now / 1000UL);
        //Serial.print("s pour ");
        //Serial.print(POMPE_PAUSE_MIN);
        //Serial.println(" min");
        pauseThermiqueLogEntree = true;
      }
      if (tempsEcoule >= (unsigned long)POMPE_PAUSE_MIN * 60UL) {
        pauseThermiqueActive = false;
        minutesPompageConsecutives = 0;
        minutesPauseRestantes = 0;
        debutPompage = 0;
        // On relance la pompe (sauf si coupée manuellement par bouton 2)
        if (!pompeManuelleOff) digitalWrite(PIN_RELAIS_POMPE, LOW);
        //Serial.print("[THERMIQUE] Fin de pause thermique à t=");
        //Serial.print(now / 1000UL);
        //Serial.println("s, pompe relancée");
        pauseThermiqueLogEntree = false;
      } else {
        digitalWrite(PIN_RELAIS_POMPE, HIGH);
      }
      // Affichage uniquement
      maj_affichage(0); // Statut Air = Oui (ou autre valeur neutre)
      return;
    } else {
      pauseThermiqueLogEntree = false;
    }

    // --- Lecture du capteur de pression (A0) ---
    //Serial.print("[PRESSION] ");
    //Serial.println(capteurs.Pression);

    debitImpulsions = pulseCount;
    pulseCount = 0;
    calculer_stats();
    // Calcul du statut Air une seule fois par seconde (retourne un indice)
    int statutAirCourant = evaluerStatutAir(true); // PompeEnMarche = true (à adapter)
    gestionAnomalieAir(statutAirCourant);

    // --- Nouvelle logique robuste pour le compteur de minutes consécutives ---
    // Critère : débit > 0 ET courant > seuil (pompe vraiment active)
    const float COURANT_POMPE_SEUIL = 0.5; // Ampères, à ajuster selon ton installation
    int nbDebitOn = 0;
    for (int i = 0; i < NBUF; i++) nbDebitOn += bufDebit[i];
    float courantMoyenne = 0;
    for (int i = 0; i < NBUF; i++) courantMoyenne += bufCourant[i];
    courantMoyenne /= NBUF;
    //bool pompeVraimentActive = (nbDebitOn > 0) && (courantMoyenne > COURANT_POMPE_SEUIL);
    bool pompeVraimentActive = (debitImpulsions > 0) && (courantMoyenne > COURANT_POMPE_SEUIL);
    static unsigned long tempsPause = 0;
    static bool enPause = false;
    static bool sortieDePause = false;
    static unsigned long tempsDernierPompageOff = 0;

    if (sortieDePause) {
      if (pompeVraimentActive) {
        debutPompage = now;
        minutesPompageConsecutives = 0;
        sortieDePause = false;
      }
    } else {
      if (pompeVraimentActive) {
        if (debutPompage == 0) {
          debutPompage = now;
          minutesPompageConsecutives = 0;
        }
        // Si la pompe était OFF récemment (<5s), on met juste en pause le compteur
        if (tempsDernierPompageOff && (now - tempsDernierPompageOff < DUREE_NON_POMPAGE_RESET_MS)) {
          // On ne fait rien, compteur en pause
        } else {
          unsigned long elapsed = now - debutPompage;
          unsigned int newMinutes = elapsed / 60000UL;
          if (newMinutes != minutesPompageConsecutives) {
            minutesPompageConsecutives = newMinutes;
          }
        }
        tempsDernierPompageOff = 0;
      } else {
        // Pompe OFF : on ne reset pas tout de suite, on attend 5s
        if (!tempsDernierPompageOff) tempsDernierPompageOff = now;
        if (now - tempsDernierPompageOff >= DUREE_NON_POMPAGE_RESET_MS) {
          debutPompage = 0;
          minutesPompageConsecutives = 0;
        }
      }
      // Déclenche la pause thermique si limite atteinte
      if (minutesPompageConsecutives >= POMPE_MAX_CONSEC_MIN) {
        pauseThermiqueActive = true;
        debutPauseThermique = now;
        minutesPauseRestantes = POMPE_PAUSE_MIN;
        digitalWrite(PIN_RELAIS_POMPE, HIGH);
        if (purgeManuelleActive) {
          purgeManuelleActive = false;
          digitalWrite(PIN_RELAIS_PURGE, LOW);
        }
      }
    }
    
    // Log état global
    //Serial.print("[ETAT GLOBAL] pauseThermiqueActive: "); //Serial.print(pauseThermiqueActive);
    //Serial.print(" | etatAnomalieAir: ");
    // switch(etatAnomalieAir) {
    //   case NORMAL: //Serial.print("NORMAL"); break;
    //   case ANOMALIE: //Serial.print("ANOMALIE"); break;
    //   case SECURITE: //Serial.print("SECURITE"); break;
    //   case ATTENTE_REDEMARRAGE: //Serial.print("ATTENTE_REDEMARRAGE"); break;
    // }
    //Serial.print(" | Pompe: "); //Serial.print(capteurs.RelaisPompe==LOW ? "ON" : "OFF");
    //Serial.print(" | Air: "); //Serial.print(statusText[statutAirCourant]);
    //Serial.print(" | P: "); //Serial.print(capteurs.RelaisPurge==LOW ? "OFF" : "ON");
    //Serial.print(" | I: "); //Serial.println(courantMoy);

    // Log du buffer débit et de debitMoy juste avant affichage LCD
    //Serial.print("[DEBUG DEBIT] bufDebit[]: ");
    // for (int i = 0; i < NBUF; i++) {
    //   //Serial.print(bufDebit[i]);
    //   //Serial.print(" ");
    // }
    //Serial.print("| debitMoy: ");
    //Serial.println(debitMoy, 3);
    maj_affichage(statutAirCourant);

    // Log de debitMoy juste avant l'envoi du message RF24
    //Serial.print("[DEBUG RF24] debitMoy: ");
    //Serial.println(debitMoy, 3);

}

void handleCommunication() {
  // Réception RF prioritaire
  static char rfMsg[32] = "";
  if (radio.available()) {
    radio.read(&rfMsg, sizeof(rfMsg));
    //Serial.print("[RF] Reçu: ");
    //Serial.println(rfMsg);
    if (strcmp(rfMsg, "PING?") == 0) {
      forceSendEtat = true;
      communiquer_chalet();
    } else if (strcmp(rfMsg, "RESET!") == 0) {
      sendRFMessage("RESET_ACK!");
      delay(100);
      NVIC_SystemReset();
    } else if (strcmp(rfMsg, "CMD1") == 0) {
      // Toggle purge manuelle (identique au bouton 1 local)
      purgeManuelleActive = !purgeManuelleActive;
      digitalWrite(PIN_RELAIS_PURGE, purgeManuelleActive ? HIGH : LOW);
      //Serial.print("[RF CMD1] Purge manuelle: "); //Serial.println(purgeManuelleActive ? "ON" : "OFF");
    } else if (strcmp(rfMsg, "CMD2") == 0) {
      // Toggle pompe manuelle OFF (identique au bouton 2 local)
      pompeManuelleOff = !pompeManuelleOff;
      digitalWrite(PIN_RELAIS_POMPE, pompeManuelleOff ? HIGH : LOW);
      //Serial.print("[RF CMD2] Pompe manuelle OFF: "); //Serial.println(pompeManuelleOff ? "ON" : "OFF");
    } else if (strcmp(rfMsg, "CMD3") == 0) {
      // Réservé bouton 3
      //Serial.println("[RF CMD3] Recu (non implémenté)");
    }
    memset(rfMsg, 0, sizeof(rfMsg));
  }
  // Envoi d'état (détection de changement + 5 répétitions max)
  communiquer_chalet();
}

void loop() {
   unsigned long now = millis();    
   // Traiter les 50ms
   if (now - dernierTraitement.t50ms > INTERVAL_50MS) { 
     if (!handleBoutonReset()) {
         handleBouton1();
         handleBouton2();
         handleBouton3();   
     }
     dernierTraitement.t50ms = now;
   }
   if (now - dernierTraitement.t100ms > INTERVAL_100MS) { 
     // Traiter les capteurs (sondeIR, Debit, Courant, Pression)
     handleCaptureSondes(); // tous les cycles
     handleSondeIr();
     handledebimetre();
     handleCourant();
     idxBuf = (idxBuf + 1) % NBUF;
     // handelePression(); // pas encore de logique pour la pression
     dernierTraitement.t100ms = now;
   }
   if (now - dernierTraitement.t1000ms > INTERVAL_1000MS) { 
     // Traiter l'affichage et la communication
     // ... à compléter : logique de gestion des capteurs
     handleAffichage(now); 
     handleCommunication();
     dernierTraitement.t1000ms = now;
   }

}

// --- Fonction centrale de gestion de l'anomalie d'air ---
void gestionAnomalieAir(int statutAir) {
  unsigned long now = millis();

  // Suivi du statut Air courant
  static int dernierStatutAirIdx = -1;
  if (statutAir != dernierStatutAirIdx) {
    dernierStatutAirIdx = statutAir;
    tDernierEtatAir = now;
    //Serial.print("[ANOMALIE AIR] Changement statut Air: ");
    //Serial.println(statusText[statutAir]);
  }

  // Affiche chaque seconde l'état et le temps passé dans ce statut
  static unsigned long lastLog = 0;
  if (now - lastLog >= 1000) {
    lastLog = now;
    //Serial.print("[ANOMALIE AIR] Statut Air: ");
    //Serial.print(statusText[statutAir]);
    //Serial.print(" | Etat global: ");
    // switch(etatAnomalieAir) {
    //   case NORMAL: //Serial.print("NORMAL"); break;
    //   case ANOMALIE: //Serial.print("ANOMALIE"); break;
    //   case SECURITE: //Serial.print("SECURITE"); break;
    //   case ATTENTE_REDEMARRAGE: //Serial.print("ATTENTE_REDEMARRAGE"); break;
    // }
    //Serial.print(" | Temps dans ce statut: ");
    //Serial.print((now - tDernierEtatAir)/1000);
    //Serial.print("s");
    if (etatAnomalieAir == ANOMALIE) {
      //Serial.print(" | Duree anomalie: ");
      //Serial.print((now - tDebutAnomalie)/1000);
      //Serial.print("s");
    }
    //Serial.println();
  }

  // Gestion des transitions d'état
  int idxStatutAir = statutAir;
  switch (etatAnomalieAir) {
    case NORMAL:
      if ((idxStatutAir == 0 && now - tDernierEtatAir >= DELAI_AIR_OUI) ||
          (idxStatutAir == 2 && now - tDernierEtatAir >= DELAI_AIR_OUIE)) {
        etatAnomalieAir = ANOMALIE;
        tDebutAnomalie = now;
        if (!purgeManuelleActive) digitalWrite(PIN_RELAIS_PURGE, HIGH); // Purge ON
      }
      // Désactive la purge dès qu'on revient à Non (eau franche)
      if (idxStatutAir == 1) {
        if (!purgeManuelleActive) digitalWrite(PIN_RELAIS_PURGE, LOW); // Purge OFF
      }
      break;
    case ANOMALIE:
      // Si retour à Non ou Non* assez longtemps, on lève l'anomalie
      if ((idxStatutAir == 1 && now - tDernierEtatAir >= DELAI_AIR_NON) ||
          (idxStatutAir == 3 && now - tDernierEtatAir >= DELAI_AIR_NONE)) {
        etatAnomalieAir = NORMAL;
        if (!purgeManuelleActive) digitalWrite(PIN_RELAIS_PURGE, LOW); // Purge OFF
        tDebutAnomalie = 0;
        // Purge les compteurs/délais d'anomalie pour éviter une rechute immédiate
        tDernierEtatAir = now;        
      }
      // Désactive la purge dès qu'on revient à Non (eau franche)
      if (idxStatutAir == 1) {
        if (!purgeManuelleActive) digitalWrite(PIN_RELAIS_PURGE, LOW); // Purge OFF
      }
      // Si anomalie > durée max, passe en sécurité
      else if (now - tDebutAnomalie >= DELAI_ANOMALIE_MAX) {
        etatAnomalieAir = SECURITE;
        tDebutSecurite = now;
        digitalWrite(PIN_RELAIS_POMPE, HIGH); // Pompe OFF
        if (!purgeManuelleActive) digitalWrite(PIN_RELAIS_PURGE, LOW);  // Purge OFF
      }
      break;
    case SECURITE:
      // Passe en attente de redémarrage manuel
      if (now - tDebutSecurite >= 0) {
        etatAnomalieAir = ATTENTE_REDEMARRAGE;
      }
      break;
    case ATTENTE_REDEMARRAGE:
      // Géré dans loop()
      break;
  }
}

// --- Fonction pour reset tous les buffers (après redémarrage manuel) ---
void resetBuffers() {
  for (int i = 0; i < NBUF; i++) {
    bufDebit[i] = 0;
    bufCourant[i] = 0;
    bufSondeIR[i] = 0;
  }
  for (int i = 0; i < NBUF_AIR; i++) bufAir[i] = 0;
  idxBuf = 0;
  idxBufAir = 0;
  // Rien d'autre ici : resetBuffers() ne doit contenir que la remise à zéro des buffers !
}

// --- Fonction d'évaluation du statut Air ---
int evaluerStatutAir(bool pompeEnMarche) {
  // Si redémarrage pompe, forcer Non pendant 10s sauf si état précédent était Oui
  static unsigned long pompeRestartTime = 0;
  static bool forceNon = false;
  if (forceNon && (millis() - pompeRestartTime < 10000)) {
    return 1; // Non
  } else {
    forceNon = false;
  }

  // Compte le nombre de détections Air dans le buffer
  int airCount = 0;
  for (int i = 0; i < NBUF_AIR; i++) airCount += bufAir[i];

  // Si la pression chute sous 125, on force Air à Oui (air détecté)
  if (capteurs.Pression < 125) {
    return 0; // Oui
  }

  if (airCount == 0) return 1; // Non
  if (airCount >= 1 && airCount <= 6) return 3; // Non*
  if (airCount >= 7 && airCount <= 9) return 2; // Oui*
  if (airCount == 10) return 0; // Oui
  // Pour 10s, airCount varie de 0 à 100, donc adapte la logique
  if (airCount <= 60) return 3; // Non*
  if (airCount <= 90) return 2; // Oui*
  return 0; // Oui
}

// Option d'affichage temporaire du airCount sur LCD
bool afficherAirCountLCD = true;

// Fonction utilitaire pour obtenir le airCount
int getAirCount() {
  int airCount = 0;
  for (int i = 0; i < NBUF_AIR; i++) airCount += bufAir[i];
  return airCount;
}

void calculer_stats() {
  // Débit
  int sumD = 0;
  for (int i = 0; i < NBUF; i++) {
    sumD += bufDebit[i];
  }
  debitMoy = sumD / (float)NBUF;

  // Courant
  float sumC = 0;
  for (int i = 0; i < NBUF; i++) {
    sumC += bufCourant[i];
  }
  courantMoy = sumC / NBUF;
  // Sonde IR (non utilisée ici)
}

//
// === UTILISATION DES 4 LIGNES LCD 20x4 ===
// Ligne 0 : Pompe:On/Off Air:Oui/Non/.. + compteur air (2 chiffres)
// Ligne 1 : Pompage:Oui/Non + compteur minutes (ex: 02/06)
// Ligne 2 : Valve:Chalet/Purge + pression (ex: Valve:Chalet   P:123)
// Ligne 3 : Débit (ex: Debit:  7/10)
//
void maj_affichage(int statutAirCourant) {
  // ...affichage LCD restauré strictement identique à l'original...
    unsigned long now = millis();
    int nbDebitOn = 0;
    for (int i = 0; i < NBUF; i++) nbDebitOn += bufDebit[i];
    float courantMoyenne = 0;
    for (int i = 0; i < NBUF; i++) courantMoyenne += bufCourant[i];
    courantMoyenne /= NBUF;
    const float COURANT_POMPE_SEUIL = 0.5; // même seuil que dans loop()
    bool pompeVraimentActive = (nbDebitOn > 0) && (courantMoyenne > COURANT_POMPE_SEUIL);
    int airCount = 0;
    for (int i = 0; i < NBUF_AIR; i++) airCount += bufAir[i];
    char airCountStr[3] = "  ";
    if (afficherAirCountLCD) {
      snprintf(airCountStr, 3, "%02d", airCount > 99 ? 99 : airCount);
    }
    int idxPompe = pauseThermiqueActive ? 5 : 4;
    const char* pompeStr = statusText[idxPompe];
    // Utiliser l'indice statutAirCourant pour l'affichage
    const char* airStr = statusText[statutAirCourant];
    char ligne0[21];
    snprintf(ligne0, 19, "Pompe:%s Air:%s", pompeStr, airStr);
    int len0 = strlen(ligne0);
    for (int i = len0; i < 18; i++) ligne0[i] = ' ';
    ligne0[18] = airCountStr[0];
    ligne0[19] = airCountStr[1];
    ligne0[20] = '\0';
    if (digitalRead(PIN_BOUTON_RESET) != LOW) {
      if (pompeManuelleOff) {
        char ligne0buf[21];
        snprintf(ligne0buf, 21, "POMPE MANUEL OFF  %02d", airCount > 99 ? 99 : airCount);
        lcd.setCursor(0, 0); lcd.print(ligne0buf);
      } else if (purgeManuelleActive) {
        char ligne0buf[21];
        snprintf(ligne0buf, 21, "PURGE MANUEL ON   %02d", airCount > 99 ? 99 : airCount);
        lcd.setCursor(0, 0); lcd.print(ligne0buf);
      } else {
        lcd.setCursor(0, 0); lcd.print(ligne0);
      }
    }

    // Ligne 1 : Pompage: Oui/Non + compteur à droite
    static bool pompageActif = false;
    static unsigned long pompageNonStart = 0;
    static bool pompageTousUn = false;
    static unsigned long pompageTousUnStart = 0;
    static const int NBUF_MINUTAGE = 63;
    static int bufMinutage[NBUF_MINUTAGE] = {0};
    static int idxBufMinutage = 0;
    static int sommeMinutage = 0;
    char ligne1[21];
    char compteurFinal[10] = "        ";
    char ligneBase[16] = "";
    bool pompageOui = false;
    static unsigned long lastAffichageSec = 0;
    static unsigned long lastPompageDebut = 0;
    static unsigned long lastPompageElapsed = 0;
    static char dernierAffichageSec[10] = "";
    unsigned long elapsedPompage = 0;
    if (pauseThermiqueActive) {
      // Affiche le temps de pause écoulé au format mm'ss/03, mais avec le label Pompage:Non*
      unsigned long now_pause = millis();
      unsigned long tempsEcoule = (now_pause - debutPauseThermique) / 1000UL;
      unsigned int min = tempsEcoule / 60;
      unsigned int sec = tempsEcoule % 60;
      char compteurPause[10];
      snprintf(compteurPause, sizeof(compteurPause), "%02u'%02u/%02u", min, sec, POMPE_PAUSE_MIN);
      snprintf(ligne1, 21, "Pompage:Non*%s", compteurPause);
      ligne1[20] = '\0';
      lcd.setCursor(0, 1); lcd.print(ligne1);
      // On sort immédiatement pour ne pas écraser l'affichage pause par le bloc compteurLCDaZero
      return;
    } else if (pompeVraimentActive) {
      pompageTousUn = false;
      pompageTousUnStart = 0;
      if (!pompageActif) {
        pompageActif = true;
        pompageNonStart = 0;
      }
      // Calcul du temps écoulé depuis le début du pompage continu
      if (lastPompageDebut == 0 || debutPompage != lastPompageDebut) {
        lastPompageDebut = debutPompage;
        lastPompageElapsed = 0;
      }
      elapsedPompage = now - debutPompage;
      // Rafraîchir les secondes toutes les 5 secondes seulement
      unsigned long elapsedSec = elapsedPompage / 1000UL;
      static unsigned long lastDisplayedSec = 0;
      if (elapsedSec / 5 != lastDisplayedSec / 5) {
        lastDisplayedSec = elapsedSec;
        unsigned int min = elapsedSec / 60;
        unsigned int sec = elapsedSec % 60;
        snprintf(dernierAffichageSec, sizeof(dernierAffichageSec), "%02u'%02u/%02u", min, sec, POMPE_MAX_CONSEC_MIN);
      }
      snprintf(ligne1, 21, "Pompage:Oui %s", dernierAffichageSec);
      pompageOui = true;
    } else if (!pompeVraimentActive && nbDebitOn == NBUF) {
      if (!pompageTousUn) {
        pompageTousUn = true;
        pompageTousUnStart = now;
      }
      if (now - pompageTousUnStart < DUREE_NON_POMPAGE_RESET_MS) {
        if (!pompageActif) {
          pompageActif = true;
          pompageNonStart = 0;
        }
        if (lastPompageDebut == 0 || debutPompage != lastPompageDebut) {
          lastPompageDebut = debutPompage;
          lastPompageElapsed = 0;
        }
        elapsedPompage = now - debutPompage;
        unsigned long elapsedSec = elapsedPompage / 1000UL;
        static unsigned long lastDisplayedSec = 0;
        if (elapsedSec / 5 != lastDisplayedSec / 5) {
          lastDisplayedSec = elapsedSec;
          unsigned int min = elapsedSec / 60;
          unsigned int sec = elapsedSec % 60;
          snprintf(dernierAffichageSec, sizeof(dernierAffichageSec), "%02u'%02u/%02u", min, sec, POMPE_MAX_CONSEC_MIN);
        }
        snprintf(ligne1, 21, "Pompage:Oui %s", dernierAffichageSec);
        pompageOui = true;
      } else {
        if (pompageActif) {
          pompageActif = false;
          minutesPompageConsecutives = 0;
        }
        snprintf(ligne1, 21, "Pompage:Non");
        pompageOui = false;
      }
    } else {
      pompageTousUn = false;
      pompageTousUnStart = 0;
      if (pompageActif) {
        if (pompageNonStart == 0) pompageNonStart = now;
        if (now - pompageNonStart > DUREE_NON_POMPAGE_RESET_MS) {
          pompageActif = false;
          minutesPompageConsecutives = 0;
        }
      }
      snprintf(ligne1, 21, "Pompage:Non");
      pompageOui = false;
    }
    static bool attente5Non = false;
    static bool compteurLCDaZero = true;
    if (pompageOui) {
      sommeMinutage -= bufMinutage[idxBufMinutage];
      bufMinutage[idxBufMinutage] = 1;
      sommeMinutage += 1;
      idxBufMinutage = (idxBufMinutage + 1) % NBUF_MINUTAGE;
      if (idxBufMinutage == 0) {
        if (sommeMinutage >= 59) {
          minutesPompageConsecutives++;
        }
        for (int i = 0; i < NBUF_MINUTAGE; i++) bufMinutage[i] = 0;
        sommeMinutage = 0;
      }
      attente5Non = false;
      compteurLCDaZero = false;
    } else {
      int nbNon = 0;
      for (int i = 0; i < 5; i++) {
        int idx = (idxBufMinutage + i) % NBUF_MINUTAGE;
        if (bufMinutage[idx] == 0) nbNon++;
      }
      if (nbNon == 5) {
        for (int i = 0; i < NBUF_MINUTAGE; i++) bufMinutage[i] = 0;
        sommeMinutage = 0;
        idxBufMinutage = 0;
        minutesPompageConsecutives = 0;
        attente5Non = false;
        compteurLCDaZero = true;
      } else {
        attente5Non = true;
      }
      if (attente5Non == false) {
        minutesPompageConsecutives = 0;
        compteurLCDaZero = true;
      }
    }
    if (compteurLCDaZero) {
      snprintf(compteurFinal, sizeof(compteurFinal), "00'00/%02u", POMPE_MAX_CONSEC_MIN);
      if (pauseThermiqueActive) {
        snprintf(ligne1, 21, "Pompage:Non* %s", compteurFinal);
      } else {
        snprintf(ligne1, 21, "Pompage:Non %s", compteurFinal);
      }
    }
    ligne1[20] = '\0';
    lcd.setCursor(0, 1); lcd.print(ligne1);

    // Affichage de la ligne 2 : Valve + pression
    const char* valve = (capteurs.RelaisPurge == HIGH) ? "Purge " : statusText[6];
    char ligne2[21];
    // 7 caractères pour Valve:..., 7 pour Chalet/Purge, 6 pour pression
    // Format: Valve:Chalet   P:123
    snprintf(ligne2, 21, "Valve:%-7sP:%4d", valve, capteurs.Pression);
    ligne2[20] = '\0';
    lcd.setCursor(0, 2); lcd.print(ligne2);

    if (afficherDebitSimple) {
      char ligne3[21];
      int nbZero = 0;
      for (int i = 0; i < NBUF; i++) if (bufDebit[i] == 0) nbZero++;
      int debitAffiche = (nbZero == 0) ? 0 : (int)(debitMoy * 10 + 0.5);
      // Affichage courant format I:5.23A, aligné à droite
      char courantStr[8];
      snprintf(courantStr, sizeof(courantStr), "I:%1.2fA", courantMoy);
      // Compose la ligne: "Debit:  7/10 I:5.23A"
      //snprintf(ligne3, 21, "Debit: %2d/10 %-7s", debitAffiche, courantStr);
      // format affichage nombre d'échantillon sur 10 avec signal hight
      // snprintf(ligne3, 21, "Debit: %2d/s %-7s", debitImpulsions, courantStr);
      // Format d'affichage du débit d/10 de 0 à 10, basé sur le nombre d'impulsions dans le buffer (0 à 10)
      snprintf(ligne3, 21, "Debit: %2d/10 %-7s", nbDebitOn, courantStr);
      ligne3[20] = '\0';
      lcd.setCursor(0, 3); lcd.print(ligne3);
    } else {
      char bufStr[11];
      for (int i = 0; i < NBUF; i++) bufStr[i] = bufDebit[(idxBuf + i) % NBUF] ? '1' : '0';
      bufStr[NBUF] = '\0';
      char ligne3[21];
      int nbZero = 0;
      for (int i = 0; i < NBUF; i++) if (bufDebit[i] == 0) nbZero++;
      int debitAffiche = (nbZero == 0) ? 0 : (int)(debitMoy * 10 + 0.5);
      char courantStr[8];
      snprintf(courantStr, sizeof(courantStr), "I:%1.2fA", courantMoy);
      snprintf(ligne3, 21, "Debit:%s %2d/10%-7s", bufStr, debitAffiche, courantStr);
      ligne3[20] = '\0';
      lcd.setCursor(0, 3); lcd.print(ligne3);
    }
}

// Variables pour l'encodage et la détection de changement
unsigned int msgSeq = 0; // Séquentiel 0-999
unsigned long lastMsgSent = 0;
unsigned int repeatCount = 0; // Nombre de répétitions du message courant (max 5)

// --- Fonction d'envoi RF ---
bool sendRFMessage(const char* msg) {
  radio.stopListening();
  bool ok = radio.write(msg, strlen(msg) + 1);
  radio.startListening();
  delay(2);
  if (ok) {
    //Serial.print("[RF24] Message envoyé: ");
    //Serial.println(msg);
  } else {
    //Serial.print("[RF24] Echec envoi: ");
    //Serial.println(msg);
  }
  return ok;
}


void communiquer_chalet() {
  // Format : SSSAPDCCPPPPMMMMFGAA  (20 chars + null)
  // SSS=seq, A=air, P=pompe, D=debit, CC=courant dix., PPPP=pression, MMMM=minutes, F=flagPurge, G=flagPompeOff, AA=airCount

  int idxAir = evaluerStatutAir(true);
  int pompeEtat = capteurs.RelaisPompe == LOW ? 4 : 5;
  int debit = debitImpulsions;
  if (debit > 9) debit = 9;
  int courantDix = (int)(courantMoy * 10.0 + 0.5);
  if (courantDix < 0) courantDix = 0;
  if (courantDix > 99) courantDix = 99;
  int pression = (int)capteurs.Pression;
  if (pression < 0) pression = 0;
  if (pression > 9999) pression = 9999;
  unsigned int minPompe = minutesPompageConsecutives;
  int flagPurge    = purgeManuelleActive ? 1 : 0;
  int flagPompeOff  = pompeManuelleOff ? 1 : 0;
  int airCount = 0;
  for (int i = 0; i < NBUF_AIR; i++) airCount += bufAir[i];
  if (airCount > 99) airCount = 99;

  char msg[24];
  snprintf(msg, sizeof(msg), "%03u%d%d%d%02d%04d%04u%d%d%02d",
           msgSeq, idxAir, pompeEtat, debit, courantDix, pression, minPompe, flagPurge, flagPompeOff, airCount);

  // Champs stables (sans courant/pression qui fluctuent)
  char msgStable[14];
  snprintf(msgStable, sizeof(msgStable), "%d%d%d%04u%d%d", idxAir, pompeEtat, debit, minPompe, flagPurge, flagPompeOff);

  static char lastMsgStable[14] = "";
  static int  lastCourantDix    = -999;
  static int  lastPression      = -9999;
  static unsigned long lastEnvoi = 0;

  unsigned long now = millis();
  bool stableChange  = (strcmp(msgStable, lastMsgStable) != 0);
  bool courantChange = abs(courantDix - lastCourantDix) >= 2; // >= 0.2A
  bool pressionChange = abs(pression - lastPression) >= 20;
  bool delaiOk = (now - lastEnvoi >= 5000UL);

  if (forceSendEtat || stableChange || (delaiOk && (courantChange || pressionChange))) {
    forceSendEtat = false;
    msgSeq = (msgSeq + 1) % 1000;
    snprintf(msg, sizeof(msg), "%03u%d%d%d%02d%04d%04u%d%d%02d",
             msgSeq, idxAir, pompeEtat, debit, courantDix, pression, minPompe, flagPurge, flagPompeOff, airCount);
    strncpy(lastMsgStable, msgStable, sizeof(lastMsgStable));
    lastCourantDix = courantDix;
    lastPression   = pression;
    repeatCount = 1;
    lastEnvoi   = now;
    lastMsgSent = now;
    //Serial.print("MSG:"); //Serial.println(msg);
    sendRFMessage(msg);
  } else if (repeatCount > 0 && repeatCount < 5 && (now - lastMsgSent >= 1000)) {
    repeatCount++;
    lastMsgSent = now;
    //Serial.print("MSG:"); //Serial.println(msg);
    sendRFMessage(msg);
  }
}
