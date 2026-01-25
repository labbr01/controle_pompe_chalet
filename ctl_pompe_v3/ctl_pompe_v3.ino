// OBSOLÈTE : Voir README_CAPTEURS.md
/*
 * ============================================================================
 * ÉTAPE 1 : RÉORGANISATION DES DÉCLARATIONS
 * Objectif : Structurer le code sans changer la logique
 * - Regroupement des constantes par thème
 * - Regroupement des variables globales par usage
 * - Commentaires de section clairs
 * ============================================================================
 */

// ============================================================================
// BIBLIOTHÈQUES
// ============================================================================
#include <LiquidCrystal.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ============================================================================
// CONFIGURATION MATÉRIELLE - PINS ET PÉRIPHÉRIQUES
// ============================================================================

// --- Écran OLED ---
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// --- LCD 16x2 (pins: RS, E, D4, D5, D6, D7) ---
const int PIN_LCD_RS = 12;
const int PIN_LCD_E = 11;
const int PIN_LCD_D4 = 5;
const int PIN_LCD_D5 = 4;
const int PIN_LCD_D6 = 3;
const int PIN_LCD_D7 = 2;
LiquidCrystal lcd(PIN_LCD_RS, PIN_LCD_E, PIN_LCD_D4, PIN_LCD_D5, PIN_LCD_D6, PIN_LCD_D7);

// --- Capteurs ---
const int PIN_PRESSION = A0;  // Manomètre analogique
const int PIN_AMPERAGE = A1;  // Capteur d'ampérage analogique
const int PIN_DEBIT = 6;      // Débitmètre à impulsions (digital)

// --- Relais ---
const int RELAIS_PURGE = 8;   // Électrovanne de purge (HIGH = ouvert)
const int RELAIS_POMPE = 7;   // Pompe (LOW = ON, HIGH = OFF)

// ============================================================================
// PARAMÈTRES AJUSTABLES
// ============================================================================
const unsigned long MAX_POMPE = 30UL * 60UL * 1000UL;        // 30 minutes max de fonctionnement continu
const unsigned long TEMPS_REPOS = 5UL * 60UL * 1000UL;       // 5 minutes de repos obligatoire après MAX_POMPE
const int SEUIL_PRESSION_PURGE = 125;                        // Pression max pour déclencher la purge
const unsigned long DUREE_PURGE = 20UL * 1000UL;             // 20 secondes de purge max
const int SEUIL_AFFICHAGE_DELTA = 10;                        // Variation min pour afficher (évite spam Serial)

// ============================================================================
// CONSTANTES DE CONFIGURATION - BUFFERS ET LISSAGE
// ============================================================================
const int TAILLE_PILE = 10;        // Taille du buffer circulaire pour le débit
const int N_LISSAGE = 10;          // Nombre de mesures pour lisser l'ampérage
const int NB_COND_PURGE = 10;      // Fenêtre glissante pour détecter la purge (10 secondes)
const int SEUIL_PURGE_ACTIVE = 5;  // Nombre de conditions remplies pour activer la purge (5/10)

// ============================================================================
// VARIABLES GLOBALES - MESURES CAPTEURS
// ============================================================================
int pression = 0;        // Valeur actuelle de la pression (brute)
unsigned int debit = 0;  // Nombre de zéros dans le buffer débit (0 = pas de débit)
int amperage = 0;        // Valeur lissée de l'ampérage

// ============================================================================
// VARIABLES GLOBALES - ÉTAT POMPE
// ============================================================================
bool pompeActive = false;           // true si la pompe est en fonctionnement
bool reposActive = false;           // true si la pompe est en période de repos
unsigned long debutPompe = 0;       // Timestamp de démarrage de la pompe
unsigned long debutRepos = 0;       // Timestamp de début du repos

// ============================================================================
// VARIABLES GLOBALES - ÉTAT PURGE
// ============================================================================
bool purgeActive = false;                  // true si une purge est en cours (mode automatique)
unsigned long debutPurge = 0;              // Timestamp de début de purge
volatile bool purgePulseEnCours = false;   // true pendant le pulse de 10s (désactive gestionAutomatique)
bool purgeBloquee = false;                 // Mode debug : bloque la purge si true

// ============================================================================
// VARIABLES GLOBALES - BUFFERS ET LISSAGE
// ============================================================================

// --- Buffer circulaire pour le débit ---
int pileEtat[TAILLE_PILE] = {0};   // Stocke les derniers états du débitmètre
int indexPile = 0;                  // Index du prochain emplacement dans la pile

// --- Buffer circulaire pour l'ampérage ---
int ampBuffer[N_LISSAGE] = {0};    // Stocke les N dernières mesures d'ampérage
int ampIndex = 0;                   // Index du prochain emplacement dans le buffer

// --- Buffer circulaire pour la détection de purge ---
bool condPurgeBuffer[NB_COND_PURGE] = {0};  // Mémorise si la condition de purge était vraie (1 par seconde)
int condPurgeIndex = 0;                      // Index du prochain emplacement
int nbTrue = 0;                              // Nombre de conditions de purge remplies dans la fenêtre

// ============================================================================
// VARIABLES GLOBALES - AFFICHAGE ET DEBUG
// ============================================================================
bool firstPrint = true;              // true pour forcer le premier affichage
bool traceInit = false;              // true une fois le message d'init affiché
int lastPression = -1;               // Dernière pression affichée (pour détection de changement)
int lastDebit = -1;                  // Dernier débit affiché
int lastAmperage = -1;               // Dernier ampérage affiché

// ============================================================================
// VARIABLES NON UTILISÉES (À NETTOYER PLUS TARD)
// ============================================================================
const int NB_PURGE_TS = 5;                   // Non utilisé actuellement
unsigned long purgeTs[NB_PURGE_TS] = {0};    // Non utilisé actuellement
int purgeTsIndex = 0;                        // Non utilisé actuellement
int lastPressionDebitZero = -1;              // Non utilisé actuellement

void setup() {
  Serial.begin(9600);
  // Initialisation OLED
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    for(;;);
  }
  // Initialisation des relais
  pinMode(RELAIS_PURGE, OUTPUT);
  pinMode(RELAIS_POMPE, OUTPUT);
  // Test manuel : active le relais purge 5 secondes pour vérifier le pilotage
  digitalWrite(RELAIS_PURGE, HIGH); // vanne ouverte (LED relais doit s'allumer)
  Serial.println("[TEST] RELAIS_PURGE HIGH (LED doit s'allumer 5s)");
  delay(5000);
  digitalWrite(RELAIS_PURGE, LOW); // vanne fermée
  Serial.println("[TEST] RELAIS_PURGE LOW (LED doit s'éteindre)");
  digitalWrite(RELAIS_POMPE, LOW); // pompe arrêtée par défaut (LOW = coupée)
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0,0);
  display.println("OLED OK!");
}


// --- Gestion automatique de la pompe et de la purge ---
void gestionAutomatique() {
    // Désactive toute la gestion centrale si pulse purge en cours
    if (purgePulseEnCours) return;
  // Mode debug : purge bloquée
  if (purgeBloquee) {
    if (digitalRead(RELAIS_PURGE) != LOW) {
      digitalWrite(RELAIS_PURGE, LOW);
      Serial.println("[ACTION] Vanne purge FERMEE (debug)");
    }
    purgeActive = false;
    static bool msgBloque = false;
    if (!msgBloque) {
      Serial.println("[DEBUG] Mode purge bloquée : vanne toujours fermée");
      msgBloque = true;
    }
    return;
  }

  unsigned long now = millis();

  // Attente remplissage buffers
  if (indexPile < TAILLE_PILE || ampIndex < N_LISSAGE) {
    if (digitalRead(RELAIS_PURGE) != LOW) {
      digitalWrite(RELAIS_PURGE, LOW);
      Serial.println("[ACTION] Vanne purge FERMEE (init)");
    }
    if (digitalRead(RELAIS_POMPE) != LOW) {
      digitalWrite(RELAIS_POMPE, LOW);
      Serial.println("[ACTION] Pompe COUPEE (init)");
    }
    if (!traceInit) {
      Serial.println("[INIT] Remplissage des buffers, relais à 0, attente avant monitoring...");
      traceInit = true;
    }
    return;
  }

  // Critères de fonctionnement
  bool demandePompe = (debit != 0);
  // Activation de la purge uniquement si au moins 5 conditions de purge dans la pile de 10
  bool demandePurge = (nbTrue >= 5); // 5 mesures sur 10 dans la pile

  // --- Gestion de la séquence de purge ---
  static bool purgeJustStarted = false;
  static bool pompeCoupeeApresPurge = false;
  if (purgeActive) {
    // Début de purge : trace unique
    if (!purgeJustStarted) {
      Serial.println("[DEBUT PURGE] Purge activée, vanne ouverte");
      purgeJustStarted = true;
      pompeCoupeeApresPurge = false;
    }
    // Pilotage du relais purge : toujours HIGH tant que purgeActive
    if (digitalRead(RELAIS_PURGE) != HIGH) {
      digitalWrite(RELAIS_PURGE, HIGH);
      Serial.println("[ACTION] Vanne purge OUVERTE (RELAIS HIGH)");
    }
    // Pompe ON sauf si coupée par sécurité
    if (!pompeCoupeeApresPurge) {
      if (digitalRead(RELAIS_POMPE) != LOW) {
        digitalWrite(RELAIS_POMPE, LOW);
        Serial.println("[ACTION] Pompe ACTIVE");
      }
    }
    // Si le débit redevient positif ou la pression remonte, on termine la purge
    if (debit > 0 || pression > SEUIL_PRESSION_PURGE) {
      purgeActive = false;
      if (digitalRead(RELAIS_PURGE) != LOW) {
        digitalWrite(RELAIS_PURGE, LOW);
        Serial.println("[ACTION] Vanne purge FERMEE (RELAIS LOW)");
      }
      Serial.println("[FIN PURGE] Débit ou pression OK, purge terminée");
      purgeJustStarted = false;
      lcd.clear(); lcd.setCursor(0,0); lcd.print("Purge finie");
      display.clearDisplay(); display.setTextSize(2); display.setCursor(0,0); display.print("Purge finie"); display.display();
      return;
    }
    // Sécurité : coupe pompe après 15s si pas d'amélioration
    if ((now - debutPurge) >= 15000 && !pompeCoupeeApresPurge) {
      if (digitalRead(RELAIS_POMPE) != HIGH) {
        digitalWrite(RELAIS_POMPE, HIGH);
        Serial.println("[ACTION] Pompe COUPEE (sécurité)");
      }
      pompeCoupeeApresPurge = true;
      Serial.println("[SECURITE] Pompe coupée après 15s de purge sans amélioration");
    }
    // Fin de purge après timeout
    if (now - debutPurge >= DUREE_PURGE) {
      purgeActive = false;
      if (digitalRead(RELAIS_PURGE) != LOW) {
        digitalWrite(RELAIS_PURGE, LOW);
        Serial.println("[ACTION] Vanne purge FERMEE (RELAIS LOW)");
      }
      Serial.println("[FIN PURGE] Durée max atteinte, purge terminée");
      purgeJustStarted = false;
      lcd.clear(); lcd.setCursor(0,0); lcd.print("Purge finie");
      display.clearDisplay(); display.setTextSize(2); display.setCursor(0,0); display.print("Purge finie"); display.display();
      return;
    }
    lcd.clear(); lcd.setCursor(0,0); lcd.print("Purge en cours");
    display.clearDisplay(); display.setTextSize(2); display.setCursor(0,0); display.print("Purge en cours"); display.display();
    return; // Pas de monitoring pendant la purge
  }

  // Trace explicite si condition de purge détectée (basée uniquement sur nbTrue)
  if (demandePurge) {
    Serial.print("[CONDITION PURGE DETECTEE] nbTrue: "); Serial.print(nbTrue); Serial.print("/"); Serial.print(NB_COND_PURGE);
    Serial.print(" | P:"); Serial.print(pression);
    Serial.print(" D:"); Serial.print(debit);
    Serial.print(" A:"); Serial.print(amperage);
    Serial.print(" | Relais purge (D8): "); Serial.print(digitalRead(RELAIS_PURGE));
    Serial.print(" | Relais pompe (D7): ");
    if (digitalRead(RELAIS_POMPE) == LOW) {
      Serial.print("LOW (moteur ON)");
    } else {
      Serial.print("HIGH (moteur OFF)");
    }
    if (purgeActive) {
      Serial.print(" | Purge déjà active");
    } else if (reposActive) {
      Serial.print(" | Repos actif, purge bloquée");
    } else if (indexPile < TAILLE_PILE || ampIndex < N_LISSAGE) {
      Serial.print(" | Buffers non remplis, purge bloquée");
    } else {
      Serial.print(" | Purge va être activée");
    }
    Serial.println();

    // Ajout : trace explicite si la purge n'est pas activée alors que la condition est remplie
    if (!purgeActive && (!reposActive && !purgeBloquee && (indexPile >= TAILLE_PILE && ampIndex >= N_LISSAGE))) {
      // Ici la purge va être activée au prochain appel, donc pas besoin de trace supplémentaire
    } else if (!purgeActive) {
      Serial.print("[INFO] Purge non activée : ");
      if (reposActive) Serial.print("repos actif, ");
      if (purgeBloquee) Serial.print("mode debug purge bloquée, ");
      if (indexPile < TAILLE_PILE || ampIndex < N_LISSAGE) Serial.print("buffers non remplis, ");
      Serial.println();
    }
  }

  // Gestion du repos
  if (reposActive) {
    if (now - debutRepos >= TEMPS_REPOS) {
      reposActive = false;
      // Affiche "Repos terminé" ou autre message
    } else {
      if (digitalRead(RELAIS_POMPE) != HIGH) {
        digitalWrite(RELAIS_POMPE, HIGH);
        Serial.println("[ACTION] Pompe COUPEE (repos)");
      }
      if (digitalRead(RELAIS_PURGE) != LOW) {
        digitalWrite(RELAIS_PURGE, LOW);
        Serial.println("[ACTION] Vanne purge FERMEE (repos)");
      }
      lcd.clear(); lcd.setCursor(0,0); lcd.print("Repos pompe");
      display.clearDisplay(); display.setTextSize(2); display.setCursor(0,0); display.print("Repos pompe"); display.display();
      return;
    }
  }

  // Gestion du temps de fonctionnement continu
  if (demandePompe) {
    if (!pompeActive) {
      debutPompe = now;
      pompeActive = true;
      Serial.println("[ACTION] Pompe ACTIVE (demande)");
    }
    if (now - debutPompe >= MAX_POMPE) {
      if (digitalRead(RELAIS_POMPE) != LOW) {
        digitalWrite(RELAIS_POMPE, LOW);
        Serial.println("[ACTION] Pompe COUPEE (max temps)");
      }
      if (digitalRead(RELAIS_PURGE) != LOW) {
        digitalWrite(RELAIS_PURGE, LOW);
        Serial.println("[ACTION] Vanne purge FERMEE (max temps)");
      }
      reposActive = true;
      debutRepos = now;
      pompeActive = false;
      lcd.clear(); lcd.setCursor(0,0); lcd.print("Repos pompe");
      display.clearDisplay(); display.setTextSize(2); display.setCursor(0,0); display.print("Repos pompe"); display.display();
      return;
    }
    // Pompe ON
    if (digitalRead(RELAIS_POMPE) != LOW) {
      digitalWrite(RELAIS_POMPE, LOW);
      Serial.println("[ACTION] Pompe ACTIVE");
    }
    // Si condition purge détectée, on lance la purge
    // La gestion de la purge est maintenant indépendante
    // Sinon, vanne purge fermée
    if (digitalRead(RELAIS_PURGE) != LOW) {
      digitalWrite(RELAIS_PURGE, LOW);
      Serial.println("[ACTION] Vanne purge FERMEE");
    }
  } else {
    // Pompe OFF
    if (digitalRead(RELAIS_POMPE) != HIGH) {
      digitalWrite(RELAIS_POMPE, HIGH);
      Serial.println("[ACTION] Pompe COUPEE");
    }
    if (digitalRead(RELAIS_PURGE) != LOW) {
      digitalWrite(RELAIS_PURGE, LOW);
      Serial.println("[ACTION] Vanne purge FERMEE");
    }
    pompeActive = false;
  }

  // Activation de la purge indépendante de la pompe
  if (demandePurge && !purgeActive) {
    if (!reposActive && !purgeBloquee && (indexPile >= TAILLE_PILE && ampIndex >= N_LISSAGE)) {
      Serial.println("[ACTION] Activation de la purge (nbTrue >= 5/10)");
      purgeActive = true;
      debutPurge = now;
      purgeJustStarted = false;
      // La gestion de la purge se fera au prochain appel
      return;
    } else {
      Serial.print("[ERREUR] Condition purge OK mais purge non activée : ");
      if (reposActive) Serial.print("repos actif, ");
      if (purgeBloquee) Serial.print("mode debug purge bloquée, ");
      if (indexPile < TAILLE_PILE || ampIndex < N_LISSAGE) Serial.print("buffers non remplis, ");
      Serial.println();
    }
  }
}

void loop() {
  // On ne gère ici que la séquence (arrêt, timeout, etc.)
  gestionAutomatique();
  static bool purgeDeclencheeLoop = false;
  static unsigned long lastMesure = 0;
  static unsigned int lastImpulsions = 0;
  unsigned long now = millis();

  pression = analogRead(PIN_PRESSION);
  // Lissage sur 10 lectures
  ampBuffer[ampIndex] = analogRead(PIN_AMPERAGE);
  ampIndex = (ampIndex + 1) % N_LISSAGE;
  long ampSum = 0;
  for (int i = 0; i < N_LISSAGE; i++) ampSum += ampBuffer[i];
  amperage = ampSum / N_LISSAGE;

  // Tampon circulaire des 100 derniers états
  int currentState = digitalRead(PIN_DEBIT);
  pileEtat[indexPile] = currentState;
  indexPile = (indexPile + 1) % TAILLE_PILE;

  // Mesure toutes les 1 seconde sans bloquer
  static bool pulsePurgeActive = false;
  static unsigned long pulsePurgeStart = 0;
  if (now - lastMesure >= 1000) {
    lastMesure = now;
    // Compte le nombre de 0 dans la pile
    int nbZero = 0;
    for (int i = 0; i < TAILLE_PILE; i++) {
      if (pileEtat[i] == 0) nbZero++;
    }
    debit = nbZero;

    // --- Mémorisation de la condition de purge sur 10 secondes (mise à jour à chaque nouvelle mesure) ---
    // Condition d'origine : pression basse ET débit nul (ou 1000)
    bool condPurge = (pression <= SEUIL_PRESSION_PURGE && (debit == 0 || debit == 1000));
    condPurgeBuffer[condPurgeIndex] = condPurge;
    condPurgeIndex = (condPurgeIndex + 1) % NB_COND_PURGE;
    nbTrue = 0;
    for (int i = 0; i < NB_COND_PURGE; i++) {
      if (condPurgeBuffer[i]) nbTrue++;
    }

    // Trace claire : uniquement le nombre de conditions remplies et l'état de condPurge
    Serial.print("[COND_PURGE] "); Serial.print(nbTrue); Serial.print("/"); Serial.print(NB_COND_PURGE);
    Serial.print(" | condPurge (remplissage buffer): "); Serial.println(condPurge ? "O" : "N");

    // Déclenchement direct du relais purge pendant 10s dès que nbTrue >= 5
    if (nbTrue >= 5 && !pulsePurgeActive) {
      Serial.println("[ACTION][LOOP] Déclenchement direct du relais purge (10s) (nbTrue >= 5/10)");
      digitalWrite(RELAIS_PURGE, HIGH);
      pulsePurgeActive = true;
      pulsePurgeStart = now;
      purgePulseEnCours = true;
    }

    // Affichage conditionnel si variation >= 10 sur au moins une valeur OU premier affichage
    static int lastPrintPression = -1000, lastPrintDebit = -1000, lastPrintAmp = -1000;
    int deltaPression = abs(pression - lastPrintPression);
    int deltaDebit = abs(debit - lastPrintDebit);
    int deltaAmp = abs(amperage - lastPrintAmp);
    if (firstPrint || deltaPression > 10 || deltaDebit > 10 || deltaAmp > 10) {
      Serial.print("Pression: "); Serial.print(pression);
      Serial.print("  Nb0: "); Serial.print(debit);
      Serial.print("  Amp: "); Serial.print(amperage);
      Serial.print(" | Purge: "); Serial.print(digitalRead(RELAIS_PURGE) == HIGH ? "PIN HIGH (tension présente)" : "PIN LOW (pas de tension)");
      Serial.print(" | Pompe: ");
      if (digitalRead(RELAIS_POMPE) == LOW) {
        Serial.print("PIN LOW (moteur ON)");
      } else {
        Serial.print("PIN HIGH (moteur OFF)");
      }
      Serial.println();
      lastPrintPression = pression;
      lastPrintDebit = debit;
      lastPrintAmp = amperage;
      firstPrint = false;
    }

    // Affichage LCD classique
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("P:"); lcd.print(pression); lcd.print(" D:"); lcd.print(debit);
    lcd.setCursor(0, 1);
    lcd.print("A:"); lcd.print(amperage); // Affiche l'ampérage sur la 2e ligne

    // Affichage OLED SSD1306
    display.clearDisplay();
    display.setTextSize(2);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0,0);
    display.print("P:"); display.print(pression);
    display.setCursor(0, 20);
    display.print("D:"); display.print(debit);
    display.setTextSize(2);
    display.setCursor(0, 40);
    display.print("A:"); display.print(amperage); // Affiche l'ampérage sur la 3e ligne
    display.display();
  }

  // Gestion du pulse relais purge (10s)
  static bool pulsePurgeWasActive = false;
  if (pulsePurgeActive) {
    if (now - pulsePurgeStart >= 10000UL) {
      digitalWrite(RELAIS_PURGE, LOW);
      Serial.println("[ACTION][LOOP] Fin du pulse relais purge (10s)");
      pulsePurgeActive = false;
      pulsePurgeWasActive = true;
      purgePulseEnCours = false;
    }
  } else if (pulsePurgeWasActive && nbTrue < 5) {
    // Réarmement du pulse si la condition retombe sous 5/10
    pulsePurgeWasActive = false;
  }

  // Ici tu peux ajouter d'autres capteurs ou traitements non bloquants
}

