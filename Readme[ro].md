# Controler Antena ATAS-120

Controller pe baza de Arduino care permite utilizarea antenei HF mobile Yaesu ATAS-120 cu **orice transceiver**, oferind totodata protectie mecanica superioara.

**Autor:** Adrian Florescu YO3HJV — Martie 2026

---

## Prezentare generala

Antena ATAS-120 se acorda in gama 7–50 MHz prin modificarea pozitiei motorului:
- **7–8,5 V** aplicati prin cablul coaxial → motorul ruleaza **DOWN** (creste inductanta, scade frecventa de acord)
- **>10,5 V** → motorul ruleaza **UP** (scade inductanta, creste frecventa de acord)

Controllerul genereaza tensiunea de comanda, monitorizeaza curentul motorului si tensiunea de alimentare prin senzorul **INA219**, si comanda motorul printr-un **tranzistor high-side**. Toata logica de protectie ruleaza pe un microcontroller AVR (Arduino).

---

## Hardware

| Componenta | Rol |
|------------|-----|
| Microcontroller AVR (Arduino) | Logica de control principala |
| INA219 (I²C, 0x40) | Monitorizare curent si tensiune |
| Tranzistor high-side pe `ctrlPin` (pin 11) | Comutator alimentare motor |
| LED Blue (pin 10, activ LOW) | Power / status secundar |
| LED Red (pin 12, activ LOW) | Indicator status principal |

LED-urile sunt conectate la +5 V prin rezistente de 1 kΩ (LOW = aprins).

---

## Functionalitati

- **POST interactiv** (Power-On Self Test cu participarea utilizatorului) — valideaza conectivitatea motorului inainte de a permite orice miscare
- **Control motor UP / DOWN** cu praguri de tensiune si histerezis configurabile
- **Detectie capat de cursa** — ambele sensuri, fara POKE
- **Secventa POKE (deblocare)** — impulsuri blande de curent pentru a elibera o antena gripata
- **Blocare directionala** — directia blocata ramane interzisa pana la comanda sensului opus
- **Detectie avarii:**
  - Scurtcircuit / supracurent (fault latch, necesita reset)
  - Antena / motor nedetectat (fault latch, necesita reset)
  - Tensiune alimentare in afara limitelor (recuperabila automat)

---

## Semnalizare prin LED-uri

### Legenda
| Simbol | Semnificatie |
|--------|--------------|
| Blue ON | LED albastru aprins continuu |
| Blue FAST | Albastru clipeste rapid (`fastBlueLedFlashOn` / `fastBlueLedFlashOff`) |
| Red ON | LED rosu aprins continuu |
| Red FAST | Rosu clipeste rapid (`fastRedLedFlashOn` / `fastRedLedFlashOff`) |
| Red SLOW | Rosu clipeste lent (`slowRedLedFlashOn` / `slowRedLedFlashOff`) |
| Red pulse | Impuls rosu scurt ~30 ms la inceputul fiecarui pas ON din POKE |

### Stari normale
| Stare | Blue | Red | Semnificatie |
|-------|------|-----|--------------|
| POWER ON | OFF | ON continuu | Controller alimentat, inainte de POST |
| POST | ON continuu | ON/OFF ghidat | Ghideaza utilizatorul prin auto-test |
| STANDBY | ON continuu | OFF | Repaus, gata de comenzi |
| RUN UP | FAST blink | OFF | Motorul ruleaza pe sens UP |
| RUN DOWN | FAST blink | OFF | Motorul ruleaza pe sens DOWN |
| BLOCAT | ON continuu | FAST blink | Capat de cursa atins; rosu clipeste cat timp utilizatorul tine comanda pe sensul blocat |
| POKE | FAST blink | Impulsuri scurte | Secventa de deblocare activa |

### Stari de avarie
| Avarie | Blue | Red | Semnificatie |
|--------|------|-----|--------------|
| SHORT | OFF | SLOW blink | Scurtcircuit detectat — **necesita reset** |
| NO ANTENNA | Flash rar (ON scurt / OFF lung) | OFF | Motor/antena nedetectat — **necesita reset** |
| CHECK BATTERY | 3 flash-uri rapide + pauza lunga | ON in pauza lunga | Tensiune alimentare in afara limitelor — **recuperabila** |

---

## Procedura POST (Power-On Self Test)

POST porneste automat la alimentare si trebuie finalizat inainte de functionarea normala.

1. **WAIT DOWN** — Blue ON, Red ON. Aplicati comanda DOWN (tensiune in fereastra `vDown`). Controllerul activeaza `ctrlPin` scurt.
2. **TEST DOWN** (max `postHoldTimeoutMs`) — Mentineti comanda. Curentul trebuie sa depaseasca `postMinMotorCurrent` cel putin `postQualifyMs`. Esec → fault latch.
3. **WAIT UP** — Eliberati in neutru (0 … `liliThreshV`), apoi aplicati comanda UP (tensiune ≥ `vUp`). Tensiunea de alimentare este validata aici.
4. **TEST UP** (max `postHoldTimeoutMs`) — Similar cu TEST DOWN. Tensiunea de alimentare verificata.
5. **PAUSE** (`postPauseAfterMs`) — Red ON, fara miscare → trece in **STANDBY**.

---

## Logica de rulare

### antenaUp (`runUpTick`)
- `ctrlPin` activ cat timp tensiunea de intrare ≥ `vUp`
- Eliberarea comenzii (tensiune < `vUpOff()`) opreste imediat → STANDBY
- Fereastra de stabilizare: `postSettleMs` dupa pornire, inainte de evaluarea curentului

| Conditie | Durata | Gratie | Actiune |
|----------|--------|--------|---------|
| curent ≥ `shortAntenna` | `shortDetectMs` | — | AVARIE: SHORT (latch) |
| curent ≤ `noAntennaCurrent` × 3 | consecutiv | `runNoAntennaGraceMs` | AVARIE: NO ANTENNA (latch) |
| curent ≥ `stopCurr` | `stopCurrDetectMs` | `runStopGraceMs` | STOP, `blockUp` = true |
| curent ≥ `stuckCurr` | `stuckDetectMs` | `runStuckGraceMs` | Intra in secventa POKE |
| tensiune < `vccMin` sau > `vccMax` | — | — | AVARIE: CHECK BATTERY (recuperabila) |

### antenaDown (`runDownTick`)
- `ctrlPin` activ cat timp tensiunea de intrare este in (`downMinCmdV` … `vDownOff()`)
- Eliberarea comenzii opreste imediat → STANDBY
- Tensiunea de alimentare **nu** este verificata in DOWN

| Conditie | Durata | Gratie / fereastra | Actiune |
|----------|--------|--------------------|---------|
| curent ≥ `shortAntenna` | `shortDetectMs` | — | AVARIE: SHORT (latch) |
| curent ≤ `noAntennaCurrent` × 3 | consecutiv | `runNoAntennaGraceMs` | AVARIE: NO ANTENNA (latch) |
| curent ≥ `endDownCurrent` | `endDownAlreadyQualMs` | primele `endDownAlreadyThereMs` | Deja la capat jos — STOP, `blockDown` (fara POKE) |
| curent ≥ `stuckCurr` | `stuckDetectMs` | `runStuckGraceMs` | Intra in secventa POKE |
| curent ≥ `endDownCurrent` | `endDownDetectMs` | `runStopGraceMs` | Capat de cursa jos — STOP, `blockDown` |
| curent ≥ `stopCurr` | `stopCurrDetectMs` | `runStopGraceMs` | STOP, `blockDown` |

---

## Parametri Fine Tuning

Toti parametrii sunt constante (`const`) definite la inceputul fisierului `ATAS_Release_1.ino`.

### Praguri de curent (mA)
| Variabila | Implicit | Descriere |
|-----------|----------|-----------|
| `noAntennaCurrent` | 25 | Sub acest prag → antena absenta |
| `shortAntenna` | 750 | Peste acest prag → scurtcircuit |
| `stopCurr` | 500 | Prag de oprire la capat de cursa (ambele sensuri) |
| `stuckCurr` | 280 | Prag de intrare in POKE |
| `endDownCurrent` | 400 | Prag capat de cursa jos |
| `postMinMotorCurrent` | 100 | Curent minim acceptat in POST |

### Timpi de calificare (ms)
| Variabila | Implicit | Descriere |
|-----------|----------|-----------|
| `shortDetectMs` | 50 | Durata minima pentru validarea SHORT |
| `stuckDetectMs` | 300 | Durata minima pentru validarea STUCK |
| `endDownAlreadyThereMs` | 150 | Fereastra initiala pentru detectia "deja la capat jos" |
| `endDownAlreadyQualMs` | 60 | Calificare in fereastra initiala |
| `endDownDetectMs` | 450 | Calificare capat jos in rulare normala |
| `stopCurrDetectMs` | 80 | Calificare oprire pe `stopCurr` |

### Timpi de gratie la rulare (ms)
| Variabila | Implicit | Descriere |
|-----------|----------|-----------|
| `runNoAntennaGraceMs` | 150 | Gratie inainte de detectia NO_ANTENNA |
| `runStopGraceMs` | 150 | Gratie inainte de evaluarea `stopCurr` / `endDownCurrent` |
| `runStuckGraceMs` | 400 | Gratie inainte de intrarea in POKE |

### Praguri de tensiune (V)
| Variabila | Implicit | Descriere |
|-----------|----------|-----------|
| `vccMin` | 10,20 | Tensiune minima de alimentare permisa |
| `vccMax` | 14,95 | Tensiune maxima de alimentare permisa |
| `vDown` | 8,20 | Limita superioara a ferestrei de comanda DOWN |
| `vUp` | 10,20 | Limita inferioara a comenzii UP |
| `vHystPercent` | 0,10 | Histerezis 10% pe ambele praguri de comanda |
| `downMinCmdV` | 5,00 | Prag minim hardcodat pentru recunoasterea comenzii DOWN |

### Parametri POKE
| Variabila | Implicit | Descriere |
|-----------|----------|-----------|
| `unstuckPokeNr` | 7 | Numar de cicluri de impulsuri |
| `pokeDuration` | 100 ms | Durata impuls ON per ciclu |
| `pokePause` | 75 ms | Pauza intre impulsuri |

### Timpi POST (ms)
| Variabila | Implicit | Descriere |
|-----------|----------|-----------|
| `postHoldTimeoutMs` | 250 | Timeout maxim pentru tinerea comenzii in POST |
| `postQualifyMs` | 80 | Timp de calificare curent in POST |
| `postSettleMs` | 20 | Stabilizare dupa activarea `ctrlPin` |
| `postPauseAfterMs` | 200 | Pauza finala inainte de STANDBY |

### Timpi LED (ms)
| Variabila | Implicit | Descriere |
|-----------|----------|-----------|
| `fastBlueLedFlashOn/Off` | 75 / 300 | Flash rapid Blue |
| `slowBlueLedFlashOn/Off` | 500 / 300 | Flash lent Blue |
| `fastRedLedFlashOn/Off` | 75 / 300 | Flash rapid Red |
| `slowRedLedFlashOn/Off` | 500 / 300 | Flash lent Red |
| `flashSequencePause` | 400 | Pauza intre secvente de flash (ex. CHECK BATTERY) |

---

## Licenta

Proiect personal — © Adrian Florescu YO3HJV, Martie 2026.
