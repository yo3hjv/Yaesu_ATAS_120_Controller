/* ATAS-120 antenna controller

Versiunea R.1.0

Rezumat semnalizari LED (Controller ATAS)

Legenda
- Blue ON = LED albastru aprins continuu
- Blue FAST blink = albastru clipeste rapid (acelasi tipar folosit la RUN_UP / RUN_DOWN)
- Red FAST blink = rosu clipeste rapid
- Red SLOW blink = rosu clipeste lent
- Red pulse = impuls rosu foarte scurt (aprox. 30ms), repetat in timpul POKE

Stari / semnificatie

1) POWER ON (STATUS_POWER_ON)
- Blue: OFF
- Red: ON continuu
Semnificatie: controller alimentat / stare initiala.

2) POST (STATUS_POST)
- Blue: ON continuu (pe toata durata POST)
- Red: feedback "cat sa tii comanda" (controlat de logica POST)
  - Red ON: utilizatorul trebuie sa tina comanda in pozitia de test (sau este intre etape dupa revenirea in zero)
  - Red OFF: timpul necesar etapei curente s-a implinit; rosu ramane stins pana cand comanda revine in zero (0..1V)
Semnificatie: ghideaza utilizatorul prin timpii POST.

3) STANDBY (STATUS_STAND_BY)
- Blue: ON continuu
- Red: OFF
Semnificatie: repaus, gata de comenzi.

4) RULARE UP (STATUS_ANTENNA_UP)
- Blue: FAST blink
- Red: OFF
Semnificatie: motorul ruleaza pe sens UP.

5) RULARE DOWN (STATUS_ANTENNA_DOWN)
- Blue: FAST blink
- Red: OFF
Semnificatie: motorul ruleaza pe sens DOWN.

6) BLOCAT / capat de cursa (STATUS_BLOCKED)
- Blue: ON continuu
- Red: FAST blink cat timp utilizatorul tine comanda pe sensul blocat
Semnificatie: s-a ajuns la capat de cursa pe directia respectiva; directia ramane blocata pana la schimbarea sensului. Ofera feedback imediat chiar daca utilizatorul continua sa tina comanda.

7) POKE (STATUS_ANTENNA_POKE)
- Blue: FAST blink (ca la rulare)
- Red: impulsuri scurte (aprox. 30ms) la inceputul fiecarui pas "ON" din POKE
Semnificatie: controllerul executa secventa de deblocare (POKE).

Semnalizari de avarie

8) Avarie SHORT (STATUS_AVARIE_SHORT_ANTENNA)
- Blue: OFF
- Red: SLOW blink
Semnificatie: scurtcircuit / supracurent detectat (fault latch).

9) Avarie NO ANTENNA (STATUS_AVARIE_NO_ANTENNA)
- Blue: un flash rar (ON scurt, OFF lung)
- Red: OFF
Semnificatie: antena/motor ne-detectat (curent prea mic / lipsa).

10) Avarie CHECK BATTERY (STATUS_AVARIE_CHECK_BATTERY)
- Blue: 3 flash-uri rapide, apoi o pauza lunga; repetitiv
- Red: ON doar in pauza lunga (OFF in pauzele scurte dintre cele 3 flash-uri)
Semnificatie: tensiune alimentare/baterie in afara limitelor permise (recuperabila cand tensiunea revine valida).

Copyright Adrian Florescu YO3HJV martie 2026

*/


#include <Wire.h>
#include <Adafruit_INA219.h>

Adafruit_INA219 ina219; // adresa implicită 0x40

float tensiune_V = 0;
int   curent_mA = 0;

static float tensiuneRaw_V = 0;
static int   curentRaw_mA = 0;

const int ctrlPin = 11;
const bool ctrlPinInverted = false;
// LEDurile sunt conectate la Vcc +5V prin rezistente de 1KOhm
const int blueLedPin = 10;  // LED Power
const int redLedPin = 12;   // LED Status
                            // Curent maxim prin 7808 = 800 mA
const int noAntennaCurrent = 25;  // Pragul maximal sub care se considera ca antena nu este conectata
const int shortAntenna = 750;     // Pragul minimal de la care se considera ca borna de antena (spre motor) este in scurtcircuit
const int stopCurr = 500;  // default=700mA, pragul maximal de la care nu se mai executa nimic la tensiunea selectata de utilizator; ctrlPin se mai deschide doar daca se schimba sensul
const int stuckCurr = 280;  // default = 350mA, pragul de la care da impulsuri de deblocare, doar pe sensul de urcare!
const int endDownCurrent = 400; // Curentul maxim cand antena este coborata.

const unsigned long shortDetectMs = 50; // cat timp trebuie mentinut curentul peste shortAntenna ca sa fie considerat SHORT
const unsigned long stuckDetectMs = 300; // cat timp trebuie mentinut curentul peste stuckCurr ca sa fie considerat STUCK (anti false-positive)

const unsigned long endDownAlreadyThereMs = 150; // daca imediat dupa pornire curentul sare peste endDownCurrent, consideram antena deja la capat jos
const unsigned long endDownDetectMs = 450;       // calificare capat jos (mai lunga decat stuckDetectMs) ca sa permita POKE la obstructie
const unsigned long endDownAlreadyQualMs = 60;   // calificare in fereastra endDownAlreadyThereMs (anti spike)
const unsigned long stopCurrDetectMs = 80;       // calificare oprire pe stopCurr (anti spike)

const float vccMin = 10.2;    // tensiunea minima, in V sub care functionarea nu se permite sau se opreste
const float vccMax = 14.95;    // tensiunea maxima, in V, peste care functionarea nu se permite sau se opreste

// Praguri pentru directia selectata de utilizator (masurate prin INA219)
const float vDown = 8.2;      // sub acest prag se considera comanda DOWN
const float vUp = 10.2;        // peste acest prag se considera comanda UP
// Histerezis pentru praguri (procent din prag); util impotriva zgomotului
const float vHystPercent = 0.10; // 10% (seteaza intre 0.07..0.10 dupa nevoie)

const float downMinCmdV = 5.0; // prag minim hardcodat pentru a considera ca exista comanda DOWN (0V nu este DOWN)

const int fastBlueLedFlashOn = 75; // perioada de ON in flash rapid, led Blue
const int fastBlueLedFlashOff = 300; // perioada de OFF in flash rapid, led Blue
const int slowBlueLedFlashOn = 500; // perioada de ON in flash lent, led Blue
const int slowBlueLedFlashOff = 300; // perioada de OFF in flash lent, led Blue

const int fastRedLedFlashOn = 75; // perioada de ON in flash rapid, led Red
const int fastRedLedFlashOff = 300; // perioada de OFF in flash rapid, led Red
const int slowRedLedFlashOn = 500; // perioada de ON in flash lent, led Red
const int slowRedLedFlashOff = 300; // perioada de OFF in flash ;ent, led Red

const int flashSequencePause = 400;  //perioada de pauza intre secvente de flash rapid sau lent

const int unstuckPokeNr = 7;    // numarul de cicluri de actionari prin ctrlPin sub conditia current_mA>stuckCurrent
const int pokeDuration = 100;    // cat timp ctrlPin este in HIGH cu conditia curent_mA>stuckCurrent
const int pokePause = 75;      // pauza intre Poke

bool vccValid = true;          // devine false cand se detecteaza tensiune_V>vccMax sau tensiune_V<vccMin
bool ctrlPinActive = true;    // devine false cand se detecteaza o conditie de AVARIE
bool sensDown = true;         // variabila booleana care indica in ce sens se misca motorul; true pentru in jos, DOWN, false pentru in sus, UP
bool antPresent = true;       // true daca curentul la iesire este mai mic de 25mA
bool shortOut = false;        //  true daca curentul la iesire este mai mare de 700mA
bool stuck = false;           // true daca curentul a depasit 350mA
bool endDown = false;         // true daca curentul a depasit 300mA

static bool motorOn = false;

static bool blockUp = false;
static bool blockDown = false;

static const uint8_t inaAvgN = 5;
static float inaVBuf[inaAvgN] = {0};
static int inaIBuf[inaAvgN] = {0};
static uint8_t inaBufIdx = 0;
static uint8_t inaBufFill = 0;
static float inaVSum = 0;
static long inaISum = 0;

bool standBy = true;           // true cand tensiune_V este intre 0 si 1V
bool lilyWasHere = false;       // devine true cand comutatorul de directie trece prin "zero" V

// functiile principale: antenaUp() si antenaDown(), urmeaza a fi definite

// Helper: tipărește un int pe 4 caractere (spații la stânga dacă e nevoie)
static void printWidth4(int val) {
        char buf[8];
        sprintf(buf, "%4d", val); // %d e suportat pe AVR
        Serial.print(buf);
}

// Helper: tipărește tensiunea cu exact 2 zecimale, fără printf %f
static void printFloat2(float v) {
          if (isnan(v) || isinf(v)) {
            Serial.print("?"); // conform cerinței, afișăm "?" dacă e invalid
            return;
          }
          // Folosim dtostrf pentru control strict (alternativ: Serial.print(v, 2))
          char vb[12];
          dtostrf(v, 0, 2, vb); // 2 zecimale, fără padding
          Serial.print(vb);
}

// --- configurare task-uri / timere ---
const unsigned long inaSamplePeriodMs = 10;     // sampling curent/tensiune (cat mai rapid)
const unsigned long serialPrintPeriodMs = 100;  // throttling debug serial

// --- POST ---
const unsigned long postMinIndicationMs = 1000; // durata minima feedback LED la pornire
const unsigned long postTestMs = 300;           // durata testului de UP / DOWN in POST
const unsigned long postPauseAfterMs = 200;     // pauza dupa testele POST
const unsigned long postHoldTimeoutMs = 250;    // timeout maxim cat timp utilizatorul tine comanda in POST

const int postMinMotorCurrent = 100;            // curent minim pentru a considera ca motorul/antena sunt conectate in POST
const unsigned long postQualifyMs = 80;         // cat timp trebuie mentinut curentul peste prag pentru a valida POST
const unsigned long postSettleMs = 20;          // timp de stabilizare dupa activarea ctrlPin inainte de evaluarea curentului
const unsigned long runNoAntennaGraceMs = 150;  // fereastra de gratie dupa pornirea motorului in RUN, inainte de a permite AVARIE_NO_ANTENNA
const unsigned long runStopGraceMs = 150;       // fereastra de gratie dupa pornirea motorului in RUN, inainte de a permite stopCurr/endDownCurrent
const unsigned long runStuckGraceMs = 400;      // fereastra de gratie dupa pornirea motorului in RUN_UP, inainte de a permite intrarea in POKE (stuck)

// --- praguri standby ---
const float standbyMinV = 0.0;
const float liliThreshV = 1.8;

// --- debug serial ---
#define SERIAL_DEBUG
// Separat: debug pentru masuratori (tensiune/curent). Lasa 0 pentru log concis.
#define SERIAL_DEBUG_MEAS 1

enum ControllerState {
  ST_BOOT = 0,
  ST_POST_WAIT_DOWN,
  ST_POST_TEST_DOWN,
  ST_POST_WAIT_UP,
  ST_POST_TEST_UP,
  ST_POST_PAUSE,
  ST_STANDBY,
  ST_RUN_UP,
  ST_RUN_DOWN,
  ST_POKE,
  ST_FAULT_LATCHED,
  ST_BATTERY_FAULT
};

enum StatusLed {
  STATUS_POWER_ON = 0,
  STATUS_POST,
  STATUS_STAND_BY,
  STATUS_ANTENNA_UP,
  STATUS_ANTENNA_DOWN,
  STATUS_AVARIE_SHORT_ANTENNA,
  STATUS_AVARIE_NO_ANTENNA,
  STATUS_AVARIE_CHECK_BATTERY,
  STATUS_ANTENNA_POKE,
  STATUS_BLOCKED
};

static ControllerState state = ST_BOOT;
static StatusLed statusLed = STATUS_POWER_ON;

static unsigned long tNow = 0;
static unsigned long lastInaSampleMs = 0;
static unsigned long lastSerialPrintMs = 0;

static unsigned long postStartMs = 0;
static unsigned long stateStartMs = 0;

static bool faultLatched = false;
static bool batteryFault = false;
static bool postSawMinCurrentDown = false; // pentru POST: calificare antena prezenta (minim ~125mA)
static bool postSawMinCurrentUp = false;   // pentru POST: calificare antena prezenta (minim ~125mA)
static int postMaxCurrentDown = 0;
static int postMaxCurrentUp = 0;
static unsigned long postQualAccumMs = 0;
static unsigned long postQualLastMs = 0;
static bool postRedOn = true;
static bool postQualified = false;
static bool postNeedsNeutral = false;

// --- POKE state ---
static int pokeStep = 0;               // 0..(unstuckPokeNr*2-1)
static unsigned long pokeStepStartMs = 0;
static bool pokeFromDown = false;

// --- LED engine ---
static bool blueLedOn = false;
static bool redLedOn = false;

static void setCtrlPin(bool on) {
  bool physOn = ctrlPinInverted ? !on : on;
  digitalWrite(ctrlPin, physOn ? HIGH : LOW);
  motorOn = on;

#ifdef SERIAL_DEBUG
  static int lastPin = -1;
  int cur = physOn ? 1 : 0;
  if (cur != lastPin) {
    lastPin = cur;
    Serial.print("ctrlPin(logic)=");
    Serial.print(on ? "ON" : "OFF");
    Serial.print(" phys=");
    Serial.println(cur ? "HIGH" : "LOW");
  }
#endif
}

static inline bool isStandbyVoltage(float v) {
  return (v >= standbyMinV) && (v <= liliThreshV);
}

static inline bool isBatteryValid(float v) {
  return (v >= vccMin) && (v <= vccMax);
}

static float vUpOn() {
  // Prag de intrare in comanda UP
  return vUp;
}

static float vUpOff() {
  // Prag de iesire din comanda UP (deadband la coborare)
  return vUp - (vUp * vHystPercent);
}

static float vDownOn() {
  // Prag de intrare in comanda DOWN
  return vDown;
}

static float vDownOff() {
  // Prag de iesire din comanda DOWN (deadband la urcare)
  return vDown + (vDown * vHystPercent);
}

static inline bool userWantsUp(float v) {
  // Comanda UP recunoscuta doar peste pragul de intrare
  return (v >= vUpOn());
}

static inline bool userWantsDown(float v) {
  // Comanda DOWN: in practica, pragul observat poate urca pana la ~8.5V;
  // folosim vDownOff() ca fereastra superioara permisiva.
  return (v >= downMinCmdV) && (v <= vDownOff());
}

static void updateFaultFlagsFromMeasurements() {
  vccValid = isBatteryValid(tensiuneRaw_V);
  static uint8_t noAntCnt = 0;
  static unsigned long shortSinceMs = 0;
  static unsigned long stuckSinceMs = 0;

  if (!motorOn) {
    noAntCnt = 0;
    shortSinceMs = 0;
    stuckSinceMs = 0;
  }

  if (motorOn) {
    if (curentRaw_mA >= shortAntenna) {
      if (shortSinceMs == 0) shortSinceMs = tNow;
    } else {
      shortSinceMs = 0;
    }

    if (curent_mA <= noAntennaCurrent) {
      if (noAntCnt < 3) noAntCnt++;
    } else {
      noAntCnt = 0;
    }

    if (curentRaw_mA >= stuckCurr) {
      if (stuckSinceMs == 0) stuckSinceMs = tNow;
    } else {
      stuckSinceMs = 0;
    }
  }

  antPresent = (noAntCnt < 3);
  shortOut = (shortSinceMs != 0) && ((tNow - shortSinceMs) >= shortDetectMs);
  stuck = (stuckSinceMs != 0) && ((tNow - stuckSinceMs) >= stuckDetectMs);
  endDown = (curentRaw_mA >= endDownCurrent);
}

static void latchFaultNoAntenna() {
  faultLatched = true;
  ctrlPinActive = false;
  setCtrlPin(false);
  statusLed = STATUS_AVARIE_NO_ANTENNA;
  state = ST_FAULT_LATCHED;
  stateStartMs = tNow;

#ifdef SERIAL_DEBUG
  Serial.println("FAULT_LATCHED: AVARIE_NO_ANTENNA");
#endif
}

static void latchFaultShort() {
  faultLatched = true;
  ctrlPinActive = false;
  setCtrlPin(false);
  statusLed = STATUS_AVARIE_SHORT_ANTENNA;
  state = ST_FAULT_LATCHED;
  stateStartMs = tNow;

#ifdef SERIAL_DEBUG
  Serial.print("FAULT_LATCHED: AVARIE_SHORT_ANTENNA Iraw=");
  printWidth4(curentRaw_mA);
  Serial.print(" mA Iavg=");
  printWidth4(curent_mA);
  Serial.println(" mA");
#endif
}

 static void latchFaultBatteryPost() {
  faultLatched = true;
  ctrlPinActive = false;
  setCtrlPin(false);
  statusLed = STATUS_AVARIE_CHECK_BATTERY;
  state = ST_FAULT_LATCHED;
  stateStartMs = tNow;

#ifdef SERIAL_DEBUG
  Serial.println("FAULT_LATCHED: AVARIE_CHECK_BATTERY (POST)");
#endif
 }

static void enterBatteryFault() {
  batteryFault = true;
  setCtrlPin(false);
  statusLed = STATUS_AVARIE_CHECK_BATTERY;
  state = ST_BATTERY_FAULT;
  stateStartMs = tNow;
}

static void exitBatteryFault() {
  batteryFault = false;
  // Revine in STANDBY; utilizatorul trebuie sa comande iar (in practica, trece prin 0..1V)
  state = ST_STANDBY;
  statusLed = STATUS_STAND_BY;
  stateStartMs = tNow;
}

static void InaSensor() {
  // Bus Voltage = tensiunea la VIN- fata de GND (tensiunea la sarcina)
  float vRaw = ina219.getBusVoltage_V();
  int iRaw = (int)lround(ina219.getCurrent_mA());

  tensiuneRaw_V = vRaw;
  curentRaw_mA = iRaw;

  if (inaBufFill < inaAvgN) {
    inaVBuf[inaBufIdx] = vRaw;
    inaIBuf[inaBufIdx] = iRaw;
    inaVSum += vRaw;
    inaISum += iRaw;
    inaBufIdx = (uint8_t)((inaBufIdx + 1) % inaAvgN);
    inaBufFill++;
  } else {
    inaVSum -= inaVBuf[inaBufIdx];
    inaISum -= inaIBuf[inaBufIdx];
    inaVBuf[inaBufIdx] = vRaw;
    inaIBuf[inaBufIdx] = iRaw;
    inaVSum += vRaw;
    inaISum += iRaw;
    inaBufIdx = (uint8_t)((inaBufIdx + 1) % inaAvgN);
  }

  uint8_t n = (inaBufFill == 0) ? 1 : inaBufFill;
  tensiune_V = inaVSum / (float)n;
  curent_mA = (int)lround((float)inaISum / (float)n);
}

static void resetInaAveraging() {
  inaBufIdx = 0;
  inaBufFill = 0;
  inaVSum = 0;
  inaISum = 0;
}

static void serialDebugTick() {
#ifdef SERIAL_DEBUG
  // Event-based: printam doar la schimbari, ca sa nu facem spam.
  static int lastState = -1;
  static uint8_t lastFlags = 0;
  static int lastCur = 0;
  static float lastV = 0;

  uint8_t flags = 0;
  if (motorOn) flags |= 0x01;
  if (vccValid) flags |= 0x02;
  if (lilyWasHere) flags |= 0x04;
  if (sensDown) flags |= 0x08;
  if (blockUp) flags |= 0x10;
  if (blockDown) flags |= 0x20;
  if (shortOut) flags |= 0x40;

  bool stateChanged = ((int)state != lastState);
  bool flagsChanged = (flags != lastFlags);

  // In modul fara masuratori, logam doar la schimbari de state/flag.
  // In modul cu masuratori, logam masuratorile doar la schimbari de state sau cand se schimba semnificativ.
  bool measChanged = false;
#if SERIAL_DEBUG_MEAS
  if (abs(curent_mA - lastCur) >= 25) measChanged = true;
  if (fabs(tensiune_V - lastV) >= 0.25f) measChanged = true;
#endif

  if (!stateChanged && !flagsChanged && !measChanged) {
    return;
  }

  lastState = (int)state;
  lastFlags = flags;
  lastCur = curent_mA;
  lastV = tensiune_V;

#if SERIAL_DEBUG_MEAS
  Serial.print("Tensiune: ");
  printFloat2(tensiune_V);
  Serial.print(" V    Curent: ");
  printWidth4(curent_mA);
  Serial.print(" mA");
#else
  Serial.print("motorOn=");
  Serial.print(motorOn ? "1" : "0");
#endif

  Serial.print("    vccValid=");
  Serial.print(vccValid ? "1" : "0");
  Serial.print("    lilyWasHere=");
  Serial.print(lilyWasHere ? "1" : "0");
  Serial.print("    sensDown=");
  Serial.print(sensDown ? "1" : "0");
  Serial.print("    blockUp=");
  Serial.print(blockUp ? "1" : "0");
  Serial.print("    blockDown=");
  Serial.print(blockDown ? "1" : "0");
  Serial.print("    shortOut=");
  Serial.print(shortOut ? "1" : "0");
  Serial.print("    state=");
  Serial.println((int)state);
#endif
}

static void ledWrite(bool blueOn, bool redOn) {
  blueLedOn = blueOn;
  redLedOn = redOn;
  digitalWrite(blueLedPin, blueOn ? LOW : HIGH); // LED la +5V => LOW aprins
  digitalWrite(redLedPin, redOn ? LOW : HIGH);
}

static void ledTick() {
  // Pentru LED-urile legate la +5V prin rezistenta, comanda este inversata:
  // LOW => aprins, HIGH => stins.

  switch (statusLed) {
    case STATUS_POWER_ON: {
      // POWER ON - LED rosu aprins permanent
      ledWrite(false, true);
      break;
    }

    case STATUS_POST: {
      // POST: Blue ON, Red controlat de logica POST (postRedOn)
      ledWrite(true, postRedOn);
      break;
    }

    case STATUS_STAND_BY: {
      // STAND_BY: LED Blue aprins continuu
      ledWrite(true, false);
      break;
    }

    case STATUS_ANTENNA_UP: {
      // ANTENNA_UP: LED BLUE Flash Rapid
      unsigned long slot = tNow % (fastBlueLedFlashOn + fastBlueLedFlashOff);
      bool on = (slot < (unsigned long)fastBlueLedFlashOn);
      ledWrite(on, false);
      break;
    }

    case STATUS_ANTENNA_DOWN: {
      // ANTENNA_DOWN: LED BLUE Flash Rapid
      unsigned long slot = tNow % (fastBlueLedFlashOn + fastBlueLedFlashOff);
      bool on = (slot < (unsigned long)fastBlueLedFlashOn);
      ledWrite(on, false);
      break;
    }

    case STATUS_AVARIE_SHORT_ANTENNA: {
      // AVARIE_SHORT_ANTENNA: Led Blue stins, red slow flash
      unsigned long slot = tNow % (slowRedLedFlashOn + slowRedLedFlashOff);
      bool on = (slot < (unsigned long)slowRedLedFlashOn);
      ledWrite(false, on);
      break;
    }

    case STATUS_AVARIE_NO_ANTENNA: {
      // AVARIE_NO_ANTENNA: fastBlueLedFlashOn, alternativ cu 8 perioade fastBlueLedFlashOff
      const unsigned long onMs = fastBlueLedFlashOn;
      const unsigned long offMs = (unsigned long)fastBlueLedFlashOff * 8UL;
      unsigned long slot = tNow % (onMs + offMs);
      bool on = (slot < onMs);
      ledWrite(on, false);
      break;
    }

    case STATUS_AVARIE_CHECK_BATTERY: {
      // AVARIE_CHECK_BATTERY: 3 secvente fastBlueLedFlashOn/fastBlueLedFlashOff urmate de flashSequencePause, repeat
      const unsigned long unit = (unsigned long)fastBlueLedFlashOn + (unsigned long)fastBlueLedFlashOff;
      const unsigned long seq = 3UL * unit;
      const unsigned long total = seq + (unsigned long)flashSequencePause;
      unsigned long t = tNow % total;

      bool on = false;
      if (t < seq) {
        unsigned long within = t % unit;
        on = (within < (unsigned long)fastBlueLedFlashOn);
      } else {
        on = false;
      }
      bool inLongPause = (t >= seq);
      ledWrite(on, inLongPause);
      break;
    }

    case STATUS_ANTENNA_POKE: {
      // ANTENNA_POKE: Blue flash rapid (ca la rulare), Red impulsuri scurte (indicatie POKE)
      unsigned long slot = tNow % (fastBlueLedFlashOn + fastBlueLedFlashOff);
      bool blueOnNow = (slot < (unsigned long)fastBlueLedFlashOn);

      // Impuls scurt pe inceputul fiecarui pas ON din POKE.
      const unsigned long pulseMs = 30;
      bool isOnStep = ((pokeStep % 2) == 0);
      bool redOnNow = isOnStep && ((tNow - pokeStepStartMs) < pulseMs);
      ledWrite(blueOnNow, redOnNow);
      break;
    }

    case STATUS_BLOCKED: {
      // Sens blocat la capat de cursa: Blue ON, Red fast flash cat timp comanda e mentinuta
      unsigned long slot = tNow % ((unsigned long)fastRedLedFlashOn + (unsigned long)fastRedLedFlashOff);
      bool redOnNow = (slot < (unsigned long)fastRedLedFlashOn);
      ledWrite(true, redOnNow);
      break;
    }

    default: {
      ledWrite(false, false);
      break;
    }
  }
}

static void startPost() {
  postStartMs = tNow;
  stateStartMs = tNow;
  postSawMinCurrentDown = false;
  postSawMinCurrentUp = false;
  postMaxCurrentDown = 0;
  postMaxCurrentUp = 0;
  postQualAccumMs = 0;
  postQualLastMs = tNow;
  postRedOn = true;
  postQualified = false;
  postNeedsNeutral = false;
  statusLed = STATUS_POST;
  state = ST_POST_WAIT_DOWN;
}

static void postTick() {
  // POST interactiv: asteptam comanda DOWN, testam 100ms, apoi asteptam comanda UP, testam 100ms,
  // apoi pauza 200ms si intram in STANDBY daca totul e OK.

  if (shortOut) {
    latchFaultShort();
    return;
  }

  unsigned long elapsed = tNow - stateStartMs;

  if (state == ST_POST_WAIT_DOWN) {
    setCtrlPin(false);
    if (postNeedsNeutral) {
      if (isStandbyVoltage(tensiuneRaw_V)) {
        postNeedsNeutral = false;
        postRedOn = true;
      } else {
        postRedOn = false;
      }
      return;
    }

    postRedOn = true;
    if (userWantsDown(tensiuneRaw_V)) {
      state = ST_POST_TEST_DOWN;
      stateStartMs = tNow;
      postSawMinCurrentDown = false;
      postMaxCurrentDown = 0;
      postQualAccumMs = 0;
      postQualLastMs = tNow;
      postRedOn = true;
      postQualified = false;
      resetInaAveraging();

#ifdef SERIAL_DEBUG
      Serial.println("POST: TEST_DOWN");
#endif
    }
    return;
  }

  if (state == ST_POST_TEST_DOWN) {
    bool holding = userWantsDown(tensiuneRaw_V);
    bool timedOut = (elapsed >= postHoldTimeoutMs);

    // Daca user nu tine comanda pana la expirarea timpului, anulam etapa.
    if (!holding && !timedOut) {
      setCtrlPin(false);
      postRedOn = true;
      state = ST_POST_WAIT_DOWN;
      stateStartMs = tNow;

#ifdef SERIAL_DEBUG
      Serial.println("POST: CANCEL_DOWN -> WAIT_DOWN");
#endif
      return;
    }

    if (holding) {
      setCtrlPin(true);
      postRedOn = true;
      if (elapsed >= postSettleMs) {
        if (curentRaw_mA > postMaxCurrentDown) {
          postMaxCurrentDown = curentRaw_mA;
        }
        if (curentRaw_mA >= postMinMotorCurrent) {
          postSawMinCurrentDown = true;
          if (!postQualified) {
            unsigned long dt = tNow - postQualLastMs;
            postQualAccumMs += dt;
            if (postQualAccumMs >= postQualifyMs) {
              postQualified = true;
            }
          }
        } else {
          if (!postQualified) {
            postQualAccumMs = 0;
          }
        }
      }
      postQualLastMs = tNow;
    } else {
      setCtrlPin(false);
    }

    if (timedOut) {
      setCtrlPin(false);
      postRedOn = false;
      bool ok = postQualified;
      if (!ok || (postMaxCurrentDown <= noAntennaCurrent)) {
#ifdef SERIAL_DEBUG
        Serial.print("POST FAIL DOWN: V=");
        printFloat2(tensiune_V);
        Serial.print(" I=");
        printWidth4(curent_mA);
        Serial.print(" mA  Imax=");
        printWidth4(postMaxCurrentDown);
        Serial.println(" mA");
#endif
        latchFaultNoAntenna();
        return;
      }
      state = ST_POST_WAIT_UP;
      stateStartMs = tNow;
      postQualAccumMs = 0;
      postQualLastMs = tNow;
      postRedOn = false;
      postQualified = false;
      postNeedsNeutral = true;

#ifdef SERIAL_DEBUG
      Serial.println("POST: WAIT_UP");
#endif
    }
    return;
  }

  if (state == ST_POST_WAIT_UP) {
    setCtrlPin(false);
    if (postNeedsNeutral) {
      if (isStandbyVoltage(tensiuneRaw_V)) {
        postNeedsNeutral = false;
        postRedOn = true;
      } else {
        postRedOn = false;
      }
      return;
    }

    postRedOn = true;
    if (userWantsUp(tensiuneRaw_V)) {
      state = ST_POST_TEST_UP;
      stateStartMs = tNow;
      postSawMinCurrentUp = false;
      postMaxCurrentUp = 0;
      postQualAccumMs = 0;
      postQualLastMs = tNow;
      postRedOn = true;
      postQualified = false;
      resetInaAveraging();

#ifdef SERIAL_DEBUG
      Serial.println("POST: TEST_UP");
#endif
    }
    return;
  }

  if (state == ST_POST_TEST_UP) {
    bool holding = userWantsUp(tensiuneRaw_V);
    bool timedOut = (elapsed >= postHoldTimeoutMs);

    // Daca user nu tine comanda pana la expirarea timpului, anulam etapa.
    if (!holding && !timedOut) {
      setCtrlPin(false);
      postRedOn = true;
      state = ST_POST_WAIT_UP;
      stateStartMs = tNow;

#ifdef SERIAL_DEBUG
      Serial.println("POST: CANCEL_UP -> WAIT_UP");
#endif
      return;
    }

    if (holding) {
      setCtrlPin(true);
      postRedOn = true;
      // in POST, AVARIE_CHECK_BATTERY este verificata doar pe UP
      if (!vccValid) {
        latchFaultBatteryPost();
        return;
      }
      if (elapsed >= postSettleMs) {
        if (curentRaw_mA > postMaxCurrentUp) {
          postMaxCurrentUp = curentRaw_mA;
        }
        if (curentRaw_mA >= postMinMotorCurrent) {
          postSawMinCurrentUp = true;
          if (!postQualified) {
            unsigned long dt = tNow - postQualLastMs;
            postQualAccumMs += dt;
            if (postQualAccumMs >= postQualifyMs) {
              postQualified = true;
            }
          }
        } else {
          if (!postQualified) {
            postQualAccumMs = 0;
          }
        }
      }
      postQualLastMs = tNow;
    } else {
      setCtrlPin(false);
    }

    if (timedOut) {
      setCtrlPin(false);
      postRedOn = false;
      bool ok = postQualified;
      if (!ok || (postMaxCurrentUp <= noAntennaCurrent)) {
#ifdef SERIAL_DEBUG
        Serial.print("POST FAIL UP: V=");
        printFloat2(tensiune_V);
        Serial.print(" I=");
        printWidth4(curent_mA);
        Serial.print(" mA  Imax=");
        printWidth4(postMaxCurrentUp);
        Serial.println(" mA");
#endif
        latchFaultNoAntenna();
        return;
      }
      state = ST_POST_PAUSE;
      stateStartMs = tNow;
      postQualAccumMs = 0;
      postQualLastMs = tNow;
      postRedOn = false;
      postQualified = false;
      postNeedsNeutral = true;

#ifdef SERIAL_DEBUG
      Serial.println("POST: PAUSE");
#endif
    }
    return;
  }

  if (state == ST_POST_PAUSE) {
    setCtrlPin(false);
    // Dupa etapa UP, tinem Red OFF pana user revine in zero.
    if (postNeedsNeutral) {
      if (isStandbyVoltage(tensiuneRaw_V)) {
        postNeedsNeutral = false;
        postRedOn = true;
        stateStartMs = tNow; // pornim pauza doar dupa zero
      } else {
        postRedOn = false;
        return;
      }
    }

    postRedOn = true;
    if (elapsed >= postPauseAfterMs) {
      state = ST_STANDBY;
      statusLed = STATUS_STAND_BY;
      stateStartMs = tNow;

#ifdef SERIAL_DEBUG
      Serial.println("POST: DONE -> STANDBY");
#endif
    }
    return;
  }

  // Stuck detect si pe coborare (anti false-positive prin time-qualified stuck)
  if (((tNow - stateStartMs) >= runStuckGraceMs) && stuck) {
    setCtrlPin(false);

#ifdef SERIAL_DEBUG
    Serial.print("RUN_DOWN -> POKE stuck I=");
    printWidth4(curentRaw_mA);
    Serial.print(" mA t=");
    Serial.println((unsigned long)(tNow - stateStartMs));
#endif

    pokeStart(true);
    return;
  }
}

static void pokeStart(bool fromDown) {
  state = ST_POKE;
  statusLed = STATUS_ANTENNA_POKE;
  pokeStep = 0;
  pokeStepStartMs = tNow;
  stateStartMs = tNow;
  pokeFromDown = fromDown;
}

static void pokeTick() {
  // Secventa: (HIGH pentru pokeDuration) apoi (LOW pentru pokePause) repetat
  // La final, lilyWasHere=false pentru a impiedica reluarea pana la StandBy (0..1V)

  if (shortOut) {
    latchFaultShort();
    return;
  }

  int totalSteps = unstuckPokeNr * 2; // on/off per poke
  if (pokeStep >= totalSteps) {
    setCtrlPin(false);
    // Daca utilizatorul tine in continuare comanda, reluam automat miscarea normala.
    // Detectia capat de cursa ramane in RUN_*.
    bool wantDown = userWantsDown(tensiuneRaw_V);
    bool wantUp = userWantsUp(tensiuneRaw_V);

    if (wantDown && !blockDown) {
      lilyWasHere = false;
      sensDown = true;
      blockUp = false;
      state = ST_RUN_DOWN;
      statusLed = STATUS_ANTENNA_DOWN;
      stateStartMs = tNow;
      return;
    }
    if (wantUp && !blockUp) {
      lilyWasHere = false;
      sensDown = false;
      blockDown = false;
      state = ST_RUN_UP;
      statusLed = STATUS_ANTENNA_UP;
      stateStartMs = tNow;
      return;
    }

    // Altfel, ne oprim in STANDBY; red va fi OFF (standby), iar la capat de cursa
    // STATUS_BLOCKED va functiona daca utilizatorul tine comanda pe sensul blocat.
    lilyWasHere = false;
    sensDown = pokeFromDown ? false : true;
    state = ST_STANDBY;
    statusLed = STATUS_STAND_BY;
    stateStartMs = tNow;
    return;
  }

  bool isOnStep = ((pokeStep % 2) == 0);
  unsigned long stepDur = isOnStep ? (unsigned long)pokeDuration : (unsigned long)pokePause;

  setCtrlPin(isOnStep);
  if ((tNow - pokeStepStartMs) >= stepDur) {
    pokeStep++;
    pokeStepStartMs = tNow;
  }
}

static void runUpTick() {
  statusLed = STATUS_ANTENNA_UP;
  setCtrlPin(true);

  static unsigned long lastStartMs = 0;
  static unsigned long stopSinceMs = 0;
  if (stateStartMs != lastStartMs) {
    lastStartMs = stateStartMs;
    stopSinceMs = 0;
  }

  // Daca utilizatorul elibereaza comanda (tensiunea iese din fereastra UP), oprim imediat fara avarii.
  if (!userWantsUp(tensiuneRaw_V)) {
    setCtrlPin(false);
    state = ST_STANDBY;
    statusLed = STATUS_STAND_BY;
    stateStartMs = tNow;
    return;
  }

  // Evita folosirea esantionului INA vechi (0mA) imediat dupa activarea ctrlPin.
  // stateStartMs este setat la intrarea in ST_RUN_UP.
  const unsigned long runSettleMs = postSettleMs;
  if ((tNow - stateStartMs) < runSettleMs) {
    return;
  }

  // AVARIE Battery recuperabila
  if (!vccValid) {
    enterBatteryFault();
    return;
  }

  if (shortOut) {
    latchFaultShort();
    return;
  }

  if (((tNow - stateStartMs) >= runNoAntennaGraceMs) && !antPresent) {
    latchFaultNoAntenna();
    return;
  }

  // Protectie stopCurr: la capat de cursa (curent mare dar < short)
  if ((tNow - stateStartMs) >= runStopGraceMs) {
    if (curent_mA >= stopCurr) {
      if (stopSinceMs == 0) stopSinceMs = tNow;
    } else {
      stopSinceMs = 0;
    }
  }

  if ((stopSinceMs != 0) && ((tNow - stopSinceMs) >= stopCurrDetectMs)) {
    setCtrlPin(false);
    lilyWasHere = false;
    sensDown = true;
    blockUp = true;
    state = ST_STANDBY;
    statusLed = STATUS_STAND_BY;
    stateStartMs = tNow;

#ifdef SERIAL_DEBUG
    Serial.print("RUN_UP STOP stopCurr I=");
    printWidth4(curent_mA);
    Serial.println(" mA");
#endif
    return;
  }

  // Stuck detect: doar la urcare
  if (((tNow - stateStartMs) >= runStuckGraceMs) && stuck) {
    setCtrlPin(false);

#ifdef SERIAL_DEBUG
    Serial.print("RUN_UP -> POKE stuck I=");
    printWidth4(curentRaw_mA);
    Serial.print(" mA t=");
    Serial.println((unsigned long)(tNow - stateStartMs));
#endif

    pokeStart(false);
    return;
  }
}

static void runDownTick() {
  statusLed = STATUS_ANTENNA_DOWN;
  setCtrlPin(true);

  static unsigned long lastStartMs = 0;
  static unsigned long endDownSinceMs = 0;
  static unsigned long endDownAlreadySinceMs = 0;
  static unsigned long stopSinceMs = 0;
  if (stateStartMs != lastStartMs) {
    lastStartMs = stateStartMs;
    endDownSinceMs = 0;
    endDownAlreadySinceMs = 0;
    stopSinceMs = 0;
  }

  // Daca utilizatorul elibereaza comanda (tensiunea iese din fereastra DOWN), oprim imediat fara avarii.
  if (!userWantsDown(tensiuneRaw_V)) {
    setCtrlPin(false);
    state = ST_STANDBY;
    statusLed = STATUS_STAND_BY;
    stateStartMs = tNow;
    return;
  }

  // Evita folosirea esantionului INA vechi (0mA) imediat dupa activarea ctrlPin.
  // stateStartMs este setat la intrarea in ST_RUN_DOWN.
  const unsigned long runSettleMs = postSettleMs;
  if ((tNow - stateStartMs) < runSettleMs) {
    return;
  }

  if (shortOut) {
    latchFaultShort();
    return;
  }

  if (((tNow - stateStartMs) >= runNoAntennaGraceMs) && !antPresent) {
    latchFaultNoAntenna();
    return;
  }

  // Daca antena e deja la capat jos si utilizatorul comanda DOWN, evitam POKE:
  // oprim imediat in prima fereastra scurta dupa start.
  if ((tNow - stateStartMs) <= endDownAlreadyThereMs) {
    if (curent_mA >= endDownCurrent) {
      if (endDownAlreadySinceMs == 0) endDownAlreadySinceMs = tNow;
    } else {
      endDownAlreadySinceMs = 0;
    }
  } else {
    endDownAlreadySinceMs = 0;
  }

  if ((endDownAlreadySinceMs != 0) && ((tNow - endDownAlreadySinceMs) >= endDownAlreadyQualMs)) {
    setCtrlPin(false);
    lilyWasHere = false;
    sensDown = false;
    blockDown = true;
    state = ST_STANDBY;
    statusLed = STATUS_STAND_BY;
    stateStartMs = tNow;

#ifdef SERIAL_DEBUG
    Serial.print("RUN_DOWN STOP endDown(already) I=");
    printWidth4(curent_mA);
    Serial.println(" mA");
#endif
    return;
  }

  // Stuck detect si pe coborare (anti false-positive prin time-qualified stuck)
  if (((tNow - stateStartMs) >= runStuckGraceMs) && stuck) {
    setCtrlPin(false);

#ifdef SERIAL_DEBUG
    Serial.print("RUN_DOWN -> POKE stuck I=");
    printWidth4(curentRaw_mA);
    Serial.print(" mA t=");
    Serial.println((unsigned long)(tNow - stateStartMs));
#endif

    pokeStart(true);
    return;
  }

  // Conditie de oprire la capat jos (time-qualified)
  if ((tNow - stateStartMs) >= runStopGraceMs) {
    if (curent_mA >= endDownCurrent) {
      if (endDownSinceMs == 0) endDownSinceMs = tNow;
    } else {
      endDownSinceMs = 0;
    }
  }

  if ((endDownSinceMs != 0) && ((tNow - endDownSinceMs) >= endDownDetectMs)) {
    setCtrlPin(false);
    lilyWasHere = false;
    sensDown = false;
    blockDown = true;
    state = ST_STANDBY;
    statusLed = STATUS_STAND_BY;
    stateStartMs = tNow;

#ifdef SERIAL_DEBUG
    Serial.print("RUN_DOWN STOP endDown I=");
    printWidth4(curent_mA);
    Serial.println(" mA");
#endif
    return;
  }

  // Protectie stopCurr (si pe DOWN)
  if ((tNow - stateStartMs) >= runStopGraceMs) {
    if (curent_mA >= stopCurr) {
      if (stopSinceMs == 0) stopSinceMs = tNow;
    } else {
      stopSinceMs = 0;
    }
  }

  if ((stopSinceMs != 0) && ((tNow - stopSinceMs) >= stopCurrDetectMs)) {
    setCtrlPin(false);
    lilyWasHere = false;
    sensDown = false;
    blockDown = true;
    state = ST_STANDBY;
    statusLed = STATUS_STAND_BY;
    stateStartMs = tNow;

#ifdef SERIAL_DEBUG
    Serial.print("RUN_DOWN STOP stopCurr I=");
    printWidth4(curent_mA);
    Serial.println(" mA");
#endif
    return;
  }
}

static void standbyTick() {
  setCtrlPin(false);
  statusLed = STATUS_STAND_BY;

  // In STANDBY, lilyWasHere devine TRUE cand utilizatorul lasa comutatorul in zona 0..1V
  standBy = isStandbyVoltage(tensiuneRaw_V);
  if (standBy) {
    lilyWasHere = true;
    return;
  }

  // Daca nu suntem in 0..1V, asteptam una din cele doua tensiuni valide (UP/DOWN)
  // Zona neutra intre vDown si vUp nu ar trebui sa existe; in practica nu face nimic.

  bool wantDown = userWantsDown(tensiuneRaw_V);
  bool wantUp = userWantsUp(tensiuneRaw_V);

  if ((wantDown && blockDown) || (wantUp && blockUp)) {
    // Utilizatorul cere sensul blocat (capat de cursa): nu pornim motorul.
    statusLed = STATUS_BLOCKED;
    return;
  }

  if (!lilyWasHere) {
    // Suprasolicitare protectie: fara trecere prin 0..1V, nu reluam comenzi
    return;
  }

  if (wantDown && !blockDown) {
    lilyWasHere = false;
    sensDown = true;
    blockUp = false;
    state = ST_RUN_DOWN;
    stateStartMs = tNow;
    return;
  }

  if (wantUp && !blockUp) {
    lilyWasHere = false;
    sensDown = false;
    blockDown = false;
    state = ST_RUN_UP;
    stateStartMs = tNow;
    return;
  }

  // Daca utilizatorul cere sensul interzis (sensDown mismatch), nu facem nimic.
}

static void batteryFaultTick() {
  setCtrlPin(false);
  statusLed = STATUS_AVARIE_CHECK_BATTERY;

  if (vccValid) {
    exitBatteryFault();
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(ctrlPin, OUTPUT);
  pinMode(redLedPin, OUTPUT);
  pinMode(blueLedPin, OUTPUT);

  setCtrlPin(false);
  ledWrite(false, false);

  if (!ina219.begin()) {
#ifdef SERIAL_DEBUG
    Serial.println("Eroare: INA219 nu raspunde pe I2C.");
#endif
    // in caz de eroare INA, oprim tot; utilizatorul trebuie sa reseteze dupa remediere
    while (1) { }
  }

  // Calibrare pentru module cu shunt ~0.10Ω, domeniu 32V/2A
  ina219.setCalibration_32V_2A();

  tNow = millis();
  lastInaSampleMs = tNow;
  lastSerialPrintMs = tNow;

  statusLed = STATUS_POST;
  startPost();
}

void loop() {
  tNow = millis();

  // INA219 sampling cu prioritate
  if ((tNow - lastInaSampleMs) >= inaSamplePeriodMs) {
    lastInaSampleMs = tNow;
    InaSensor();
    updateFaultFlagsFromMeasurements();
  }

  // Debug serial throttled
  serialDebugTick();

  // Daca avem fault latch (short/no antenna), nu continuam executia; astept reset
  if (state == ST_FAULT_LATCHED) {
    setCtrlPin(false);
    ledTick();
    return;
  }

  switch (state) {
    case ST_BOOT:
      startPost();
      break;

    case ST_POST_WAIT_DOWN:
    case ST_POST_TEST_DOWN:
    case ST_POST_WAIT_UP:
    case ST_POST_TEST_UP:
    case ST_POST_PAUSE:
      postTick();
      break;

    case ST_STANDBY:
      standbyTick();
      break;

    case ST_RUN_UP:
      runUpTick();
      break;

    case ST_RUN_DOWN:
      runDownTick();
      break;

    case ST_POKE:
      pokeTick();
      break;

    case ST_BATTERY_FAULT:
      batteryFaultTick();
      break;

    default:
      state = ST_STANDBY;
      statusLed = STATUS_STAND_BY;
      stateStartMs = tNow;
      break;
  }

  // LED tick separat
  ledTick();
}
