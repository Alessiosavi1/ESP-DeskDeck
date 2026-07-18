#!/usr/bin/env python3
"""ESP-DeskDeck PC Control — trova l'ESP via mDNS e ascolta gli eventi."""

import time
import webbrowser
import json
import logging
import socket
from urllib.parse import urlparse

import requests
import pyautogui

pyautogui.FAILSAFE = True  # LASCIA ATTIVO per sicurezza

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(message)s",
    datefmt="%H:%M:%S",
)
log = logging.getLogger("deskdeck")

# ─── Config ────────────────────────────────────────────────
HOSTNAME = "esp-deskdeck.local"  # mDNS — niente scansione IP!
PROBE_PATH = "/api/meteo"
EVENTS_PATH = "/api/events"
POLL_SEC = 0.25
POLL_TIMEOUT_SEC = 2
ENABLE_OPEN_SITE = True
# ───────────────────────────────────────────────────────────

HAVE_KEYBOARD = False
try:
    import keyboard

    HAVE_KEYBOARD = True
except Exception:
    log.warning("Libreria 'keyboard' non disponibile (serve root su Linux)")


def resolve_mdns(hostname: str, timeout: float = 3.0) -> str | None:
    """Risolve un hostname mDNS (.local) in IP."""
    try:
        addrs = socket.getaddrinfo(hostname, 80, socket.AF_INET, socket.SOCK_STREAM)
        if addrs:
            return str(addrs[0][4][0])
        return None
    except OSError:
        return None


def media(action: str):
    if not HAVE_KEYBOARD:
        return
    mapping = {
        "media_play": "play/pause media",
        "media_next": "next track",
        "media_prev": "previous track",
        "media_vol_up": "volume up",
        "media_vol_down": "volume down",
        "media_mute": "volume mute",
    }
    key = mapping.get(action)
    if key:
        try:
            keyboard.send(key)
        except Exception as e:
            log.warning("keyboard.send(%s) fallito: %s", action, e)


def safe_open_url(url: str):
    url = (url or "").strip()
    if not url:
        return
    try:
        u = urlparse(url)
        if u.scheme not in ("http", "https") or not u.netloc:
            return
    except Exception:
        return
    webbrowser.open(url)


def do_action(ev: dict):
    a = ev.get("action", "")
    url = ev.get("url", "") or ""

    if a == "open_site":
        if ENABLE_OPEN_SITE:
            safe_open_url(url)
        return

    if a in (
        "media_play",
        "media_next",
        "media_prev",
        "media_vol_up",
        "media_vol_down",
        "media_mute",
    ):
        media(a)
        return

    shortcuts = {
        "ctrl_c": (["ctrl", "c"], ["ctrl", "c"]),
        "ctrl_v": (["ctrl", "v"], ["ctrl", "v"]),
        "ctrl_z": (["ctrl", "z"], ["ctrl", "z"]),
        "win_d": (["winleft", "d"], ["win", "d"]),
        "screenshot": (["winleft", "shift", "s"], ["win", "shift", "s"]),
    }
    if a in shortcuts:
        try:
            pyautogui.hotkey(*shortcuts[a][0])
        except Exception:
            try:
                pyautogui.hotkey(*shortcuts[a][1])
            except Exception as e:
                log.warning("pyautogui %s fallito: %s", a, e)
        return

    if a == "display_toggle":
        return  # gestito dall'ESP


def sync_drop_old_events(base: str) -> int:
    """Ignora eventi già accaduti prima della connessione."""
    try:
        r = requests.get(f"{base}{EVENTS_PATH}", timeout=1.5)
        r.raise_for_status()
        data = r.json()
        max_id = 0
        for ev in data.get("events", []):
            try:
                max_id = max(max_id, int(ev.get("id", 0)))
            except Exception:
                pass
        return max_id
    except Exception as e:
        log.warning("sync events fallito: %s", e)
        return 0


def listen_events(base: str):
    global last_id
    last_id = sync_drop_old_events(base)
    log.info("Trovato ESP-DeskDeck: %s", base)
    log.info("Ignoro eventi fino a id=%s. In ascolto...", last_id)

    time.sleep(0.5)

    while True:
        try:
            r = requests.get(
                f"{base}{EVENTS_PATH}",
                params={"after": last_id},
                timeout=POLL_TIMEOUT_SEC,
            )
            r.raise_for_status()
            data = r.json()
            for ev in data.get("events", []):
                eid = int(ev.get("id", 0))
                if eid > last_id:
                    last_id = eid
                    log.debug("Evento: %s", ev.get("action"))
                    do_action(ev)
        except KeyboardInterrupt:
            log.info("Uscita.")
            break
        except requests.RequestException:
            time.sleep(0.6)
            continue
        except Exception as e:
            log.warning("Errore: %s", e)
            time.sleep(0.6)
            continue

        time.sleep(POLL_SEC)


def main():
    log.info("Cerco %s via mDNS...", HOSTNAME)

    ip = resolve_mdns(HOSTNAME, timeout=3.0)

    if not ip:
        log.warning(
            "%s non trovato via mDNS.\n"
            "  Se l'ESP è sull'AP (192.168.4.1), usa IP manuale.\n"
            "  Oppure passa l'IP come argomento: %s <IP>",
            HOSTNAME,
            __file__,
        )
        return

    log.info("Trovato: %s (%s)", HOSTNAME, ip)
    base = f"http://{ip}"
    listen_events(base)


if __name__ == "__main__":
    main()
