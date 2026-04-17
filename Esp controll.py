import time
import webbrowser
import requests
import pyautogui
from concurrent.futures import ThreadPoolExecutor, as_completed
from urllib.parse import urlparse

pyautogui.FAILSAFE = False

try:
    import keyboard
    HAVE_KEYBOARD = True
except Exception:
    HAVE_KEYBOARD = False

# ---------------- CONFIG ----------------
IP_PREFIX = "192.168.1."
IP_START = 2
IP_END = 255

PROBE_PATH = "/api/meteo"
EVENTS_PATH = "/api/events"

SCAN_TIMEOUT_SEC = 0.25
SCAN_WORKERS = 80

POLL_SEC = 0.25
POLL_TIMEOUT_SEC = 2

ENABLE_OPEN_SITE = True  # <-- RIATTIVATO
# ----------------------------------------

last_id = 0

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
    if not key:
        return
    try:
        keyboard.send(key)
    except Exception:
        pass

def safe_open_url(url: str):
    url = (url or "").strip()
    if not url:
        return
    # consenti solo http/https
    try:
        u = urlparse(url)
        if u.scheme not in ("http", "https"):
            return
        if not u.netloc:
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

    if a in ("media_play","media_next","media_prev","media_vol_up","media_vol_down","media_mute"):
        media(a)
        return

    if a == "ctrl_c":
        pyautogui.hotkey("ctrl", "c"); return
    if a == "ctrl_v":
        pyautogui.hotkey("ctrl", "v"); return
    if a == "ctrl_z":
        pyautogui.hotkey("ctrl", "z"); return
    if a == "win_d":
        try:
            pyautogui.hotkey("winleft", "d")
        except Exception:
            pyautogui.hotkey("win", "d")
        return
    if a == "screenshot":
        try:
            pyautogui.hotkey("winleft", "shift", "s")
        except Exception:
            pyautogui.hotkey("win", "shift", "s")
        return

    if a == "display_toggle":
        return

def probe_ip(ip: str) -> str | None:
    url = f"http://{ip}{PROBE_PATH}"
    try:
        r = requests.get(url, timeout=SCAN_TIMEOUT_SEC)
        if r.status_code != 200:
            return None
        j = r.json()
        if isinstance(j, dict) and "citta" in j and "vista_display" in j:
            return ip
        return None
    except Exception:
        return None

def find_esp_ip() -> str | None:
    ips = [f"{IP_PREFIX}{i}" for i in range(IP_START, IP_END + 1)]
    with ThreadPoolExecutor(max_workers=SCAN_WORKERS) as ex:
        futures = {ex.submit(probe_ip, ip): ip for ip in ips}
        for fut in as_completed(futures):
            ip = fut.result()
            if ip:
                return ip
    return None

def sync_drop_old_events(base: str) -> int:
    """
    Legge una volta /api/events e imposta last_id al massimo già presente,
    così non esegue eventi vecchi accumulati durante scansione/boot.
    """
    try:
        r = requests.get(f"{base}{EVENTS_PATH}", timeout=1.5)
        r.raise_for_status()
        data = r.json()
        events = data.get("events", [])
        max_id = 0
        for ev in events:
            try:
                max_id = max(max_id, int(ev.get("id", 0)))
            except Exception:
                pass
        return max_id
    except Exception:
        return 0

def listen_events(base: str):
    global last_id

    last_id = sync_drop_old_events(base)
    print(f"Trovato ESP32: {base}")
    print(f"Sync completato. Ignoro eventi fino a id={last_id}.")
    print("Ora ascolto SOLO nuove pressioni tasti (CTRL+C per uscire).")

    time.sleep(0.5)

    while True:
        try:
            r = requests.get(
                f"{base}{EVENTS_PATH}",
                params={"after": last_id},
                timeout=POLL_TIMEOUT_SEC
            )
            r.raise_for_status()
            data = r.json()
            events = data.get("events", [])
            for ev in events:
                eid = int(ev.get("id", 0))
                if eid > last_id:
                    last_id = eid
                do_action(ev)
        except KeyboardInterrupt:
            print("\nUscita.")
            break
        except Exception:
            time.sleep(0.6)
            continue

        time.sleep(POLL_SEC)

def main():
    print(f"Scansione rapida rete {IP_PREFIX}{IP_START}-{IP_END} (silenziosa)...")
    t0 = time.time()
    ip = find_esp_ip()
    dt = time.time() - t0

    if not ip:
        print(f"ESP32 non trovato in {dt:.2f}s.")
        print("Se sei collegato all'AP dell'ESP32, l'IP è 192.168.4.1 (non 192.168.1.x).")
        return

    print(f"Trovato in {dt:.2f}s: {ip}")
    base = f"http://{ip}"
    listen_events(base)

if __name__ == "__main__":
    main()
