# ESP-DeskDeck

> **Compact, DIY macro-pad + weather display** — built around an ESP32-C3 Mini.

ESP-DeskDeck è un'alternativa fai-da-te ai macro-pad commerciali. Funge sia da **display informativo ambientale** (meteo in tempo reale su OLED) che da **macro-pad wireless a 4 tasti** configurabili via web.

## Features

- ☁️ **Meteo real-time** via Open-Meteo API su OLED 128×32
- 🎮 **4 tasti macro wireless** configurabili (media, shortcut, URL)
- 🌐 **Web UI moderna** per configurazione e dashboard meteo
- 🧠 **mDNS** — trovato automaticamente come `esp-deskdeck.local`
- 📦 **OTA updates** — aggiorna il firmware via WiFi
- 📟 **Animazioni fullscreen** sul web (sole, nuvole, pioggia, neve, temporale, nebbia)

## Hardware

| Componente | Dettagli |
|-----------|---------|
| ESP32‑C3 Mini | Pin modificabili in `config.h` |
| OLED 128×32 I2C | SSD1306, indirizzo 0x3C |
| 4x pulsanti | Collegati a GND, con pull-up interni |

### Collegamenti

| Componente | Pin ESP32 |
|-----------|-----------|
| OLED SDA | GPIO 8 |
| OLED SCL | GPIO 9 |
| Tasto 1 | GPIO 0 |
| Tasto 2 | GPIO 1 |
| Tasto 3 | GPIO 2 |
| Tasto 4 | GPIO 3 |

## Setup Rapido

### 1. Firmware (PlatformIO — raccomandato)

```bash
# Installa PlatformIO
pip install platformio

# Compila e carica
pio run -e esp32-c3-dev --target upload
```

Oppure con Arduino IDE: apri `ESP32_Meteo_C3_IP_Version9.ino`, modifica SSID/password, carica.

### 2. Script PC

```bash
pip install requests pyautogui keyboard
python pc_control.py
```

> `keyboard` potrebbe richiedere privilegi di amministratore su alcuni OS.

Lo script trova l'ESP **automaticamente** via mDNS — niente configurazione IP!

### 3. OTA Tool (Windows)

Scarica **`ESP-DeskDeck-OTA-Tool.exe`** dalla release o dalla cartella `ota-tool/` nel repo.

Basta doppio click — si apre una UI nel browser:

```bash
# O in alternativa dalla cartella del progetto:
ota-tool/ESP-DeskDeck-OTA-Tool.exe
```

1. Seleziona la board
2. Inserisci IP/hostname (default: `esp-deskdeck.local`)
3. Click su **Compila + Upload**

### 4. OTA Updates (via CLI)

Dopo il primo flashing via USB, puoi aggiornare via WiFi:

```bash
pio run -e esp32-c3-dev-ota --target upload
```

Oppure dall'Arduino IDE: seleziona "ESP32-C3 OTA" e inserisci `esp-deskdeck.local`.

## Azioni disponibili

| Azione | Descrizione |
|--------|-------------|
| `display_toggle` | Alterna OLED tra meteo e ora |
| `media_play` | Play/Pausa |
| `media_next` | Traccia successiva |
| `media_prev` | Traccia precedente |
| `media_vol_up` | Volume su |
| `media_vol_down` | Volume giù |
| `media_mute` | Muto |
| `ctrl_c` | Copia |
| `ctrl_v` | Incolla |
| `ctrl_z` | Annulla |
| `win_d` | Mostra desktop |
| `screenshot` | Screenshot |
| `open_site` | Apre un URL |

## Web UI

Apri `http://esp-deskdeck.local` nel browser:
- **`/`** — Dashboard meteo con animazioni dinamiche
- **`/tasti`** — Configurazione dei 4 tasti macro

## Struttura del progetto

```
ESP-DeskDeck/
├── platformio.ini               # PlatformIO config (2 env: USB + OTA)
├── ESP32_Meteo_C3_IP_Version9.ino  # Firmware main
├── pc_control.py                # Script PC (mDNS, non serve IP)
├── README.md
├── LICENSE
└── .github/workflows/
    └── build.yml                # CI: compila su push
```

## Licenza

Copyright © 2026 Alessio Savinelli. Tutti i diritti riservati.
