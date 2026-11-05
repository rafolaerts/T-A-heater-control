# T&A Hal Heating Control

Verwarmingstimer voor de hal, gebouwd op een **M5Stack Core Basic (Metal, ESP32)**.
Technics & Applications – code: Raf Olaerts.

## Werking
- Knop A: 1 uur verwarmen · Knop B: 2 uur · Knop C: 4 uur
- Terwijl de timer loopt, stopt elke knop de verwarming
- Scherm toont temperatuur (CHT8305) en resterende tijd (rood = actief, groen = OFF)
- Veiligheid: relais altijd UIT bij opstart; timer herstart niet na stroomuitval

## Hardware (TinyTronics)
| Onderdeel | Ref |
|---|---|
| M5Stack M5Core Metal – ESP32 IoT Development Kit | 005320 |
| M5Stack 4-Channel Relay 13.2 Module V1.1 (I2C) | 005642 |
| DFRobot CHT8305 I2C temperatuur/vochtigheid (adres 0x40) | 004608 |

I2C op SDA 21 / SCL 22. Verwarming schakelt via relais 0.

## Structuur
```
M5CoreMetal_HeatingControl/
  M5CoreMetal_HeatingControl.ino   # huidige timer-firmware
  Example.md                       # referentie: Antifreeze-module FW 3.0 (WiFi, MQTT, webinterface)
libraries/                         # volledige Arduino-bibliothekenmap (snapshot)
```

## Bouwen (Arduino IDE)
1. Board: **M5Stack-Core-ESP32** (ESP32 board package).
2. Zet in Arduino IDE de *Sketchbook location* op deze repositorymap, zodat `libraries/` gebruikt wordt
   (of kopieer de benodigde bibliotheken naar je eigen `Arduino/libraries`).
3. Open `M5CoreMetal_HeatingControl/M5CoreMetal_HeatingControl.ino` en upload.

Bibliotheken die dit project echt gebruikt:

| Bibliotheek | Versie |
|---|---|
| M5Stack (`M5Stack.h`) | 0.4.6 |
| M5Module-4Relay (`Module_4RELAY.h`) | 0.0.3 |
| CHT8305 | 0.2.3 |

De overige bibliotheken in `libraries/` zijn meegenomen als volledige snapshot van de ontwikkelomgeving.
