#!/usr/bin/env python3
"""
ESP-DeskDeck OTA Updater
Compila e carica il firmware via WiFi (OTA) in automatico.

Utilizzo:
    python ota_update.py                    # usa l'env di default (esp32-c3-dev-ota)
    python ota_update.py --board esp32-c3   # sceglie il board
    python ota_update.py --ip 192.168.1.42  # IP specifico (default: esp-deskdeck.local)
    python ota_update.py --monitor          # apre serial monitor dopo l'upload
"""

import argparse
import os
import subprocess
import sys
import json
import re
import time
from pathlib import Path


PROJECT_DIR = Path(__file__).resolve().parent
PLATFORMIO_INI = PROJECT_DIR / "platformio.ini"


def find_environments() -> dict:
    """Legge platformio.ini e trova gli env disponibili con le relative board."""
    if not PLATFORMIO_INI.exists():
        return {"esp32-c3-dev-ota": {"board": "esp32-c3-devkitm-1", "desc": "ESP32-C3 Mini (OTA)"}}

    envs = {}
    current_env = None
    with open(PLATFORMIO_INI) as f:
        for line in f:
            line = line.strip()
            m = re.match(r"\[env:(.+)\]", line)
            if m:
                current_env = m.group(1)
                envs[current_env] = {"board": "?", "desc": current_env}
            elif current_env and line.startswith("board ="):
                envs[current_env]["board"] = line.split("=", 1)[1].strip()
            elif current_env and "upload_protocol" in line and "espota" in line:
                envs[current_env]["desc"] += " (OTA)"
    return envs


def get_default_ota_env(envs: dict) -> str:
    """Trova il primo env con upload_protocol=espota."""
    for name, info in envs.items():
        if "ota" in name.lower() or "OTA" in info.get("desc", ""):
            return name
    # fallback al primo env che contiene 'ota'
    for name in envs:
        if "ota" in name.lower():
            return name
    return list(envs.keys())[0] if envs else "esp32-c3-dev-ota"


def build_firmware(env: str) -> bool:
    """Compila il firmware con PlatformIO."""
    print(f"\n⚙️  Compilo il firmware per [{env}]...")
    start = time.time()
    result = subprocess.run(
        ["pio", "run", "-e", env],
        cwd=PROJECT_DIR,
        capture_output=True,
        text=True,
    )
    elapsed = time.time() - start

    if result.returncode == 0:
        print(f"✅  Compilato in {elapsed:.1f}s")
        # Trova il .bin generato
        fw_dir = PROJECT_DIR / ".pio" / "build" / env
        bins = list(fw_dir.glob("*.bin")) if fw_dir.exists() else []
        if bins:
            print(f"📦  Firmware: {bins[0].name} ({bins[0].stat().st_size // 1024}KB)")
        return True
    else:
        # Mostra solo errori
        errors = [l for l in result.stderr.split("\n") if "error:" in l.lower()]
        print(f"❌  Compilazione fallita ({elapsed:.1f}s)")
        for e in errors[:5]:
            print(f"   {e.strip()}")
        return False


def upload_ota(env: str, ip: str, monitor: bool = False) -> bool:
    """Carica il firmware via OTA."""
    print(f"\n📡  Carico via OTA su {ip}...")

    cmd = ["pio", "run", "-e", env, "--target", "upload",
           "--upload-port", ip]

    result = subprocess.run(
        cmd,
        cwd=PROJECT_DIR,
        capture_output=True,
        text=True,
        timeout=120,
    )

    if result.returncode == 0:
        print(f"✅  OTA upload completato!")
        if result.stdout:
            # Mostra solo le ultime righe
            lines = result.stdout.strip().split("\n")
            for l in lines[-3:]:
                print(f"   {l.strip()}")
        return True
    else:
        print(f"❌  OTA fallito!")
        errors = [l for l in result.stderr.split("\n") if "error" in l.lower() or "fail" in l.lower()]
        for e in errors[:5]:
            print(f"   {e.strip()}")
        return False


def check_prerequisites() -> bool:
    """Controlla che platformio sia installato."""
    try:
        subprocess.run(["pio", "--version"], capture_output=True, check=True)
        return True
    except (subprocess.CalledProcessError, FileNotFoundError):
        print("❌  PlatformIO non trovato. Installalo con: pip install platformio")
        return False


def main():
    parser = argparse.ArgumentParser(
        description="ESP-DeskDeck OTA Updater — compila e carica il firmware via WiFi",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Esempi:
  python ota_update.py                         # Auto: OTA su esp-deskdeck.local
  python ota_update.py --ip 192.168.4.1        # Upload su IP specifico
  python ota_update.py --board esp32-c3        # Scegli board
  python ota_update.py --monitor               # + serial monitor dopo upload
        """
    )
    parser.add_argument("--ip", default="esp-deskdeck.local",
                        help="IP o hostname dell'ESP (default: esp-deskdeck.local)")
    parser.add_argument("--board", "-b", default=None,
                        help="Board ESP (es: esp32-c3, esp32-s3, esp32)")
    parser.add_argument("--monitor", "-m", action="store_true",
                        help="Apri serial monitor dopo l'upload")
    parser.add_argument("--list", "-l", action="store_true",
                        help="Mostra gli env disponibili ed esci")
    parser.add_argument("--env", "-e", default=None,
                        help="Environment PlatformIO da usare (es: esp32-c3-dev-ota)")

    args = parser.parse_args()

    if not check_prerequisites():
        sys.exit(1)

    envs = find_environments()

    if args.list:
        print("\n📋  Board disponibili:")
        print("─" * 50)
        for name, info in envs.items():
            ota = "🔄" if "ota" in name.lower() else "🔌"
            print(f"  {ota} [{name}]")
            print(f"     Board: {info['board']}")
        print()
        sys.exit(0)

    # Sceglie l'environment
    if args.env:
        env = args.env
        if env not in envs:
            print(f"❌  Env '{env}' non trovato. Usa --list per vedere quelli disponibili.")
            sys.exit(1)
    elif args.board:
        # Match board name
        matches = [n for n in envs if args.board.lower() in n.lower()]
        if not matches:
            print(f"❌  Nessun env per board '{args.board}'. Usa --list.")
            sys.exit(1)
        env = matches[0]
        if len(matches) > 1:
            ota_matches = [n for n in matches if "ota" in n.lower()]
            env = ota_matches[0] if ota_matches else matches[0]
    else:
        env = get_default_ota_env(envs)

    print(f"\n🚀  ESP-DeskDeck OTA Updater")
    print(f"📋  Env: [{env}] → {envs.get(env, {}).get('board', '?')}")
    print(f"📍  Target: {args.ip}")

    # Step 1: Build
    if not build_firmware(env):
        sys.exit(1)

    # Step 2: Upload via OTA
    if not upload_ota(env, args.ip):
        print("\n💡  Suggerimenti:")
        print("   • L'ESP è acceso e connesso al WiFi?")
        print(f"   • È raggiungibile via {args.ip}?")
        print("   • Prova: ping", args.ip)
        sys.exit(1)

    # Step 3: Monitor (opzionale)
    if args.monitor:
        print("\n🔍  Apro serial monitor...")
        subprocess.run(["pio", "device", "monitor", "-b", "115200"])

    print(f"\n✅  Fatto! ESP-DeskDeck aggiornato alla v2.0.0 🎉")


if __name__ == "__main__":
    main()
