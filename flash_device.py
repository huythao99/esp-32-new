#!/usr/bin/env python3
"""
Flash ESP32 devices with auto-incrementing SSID.
Usage: python flash_device.py [count]
  - No argument: flash 1 device
  - With count: flash multiple devices (waits for you to connect each one)
"""

import subprocess
import re
import sys
import os
from pathlib import Path

MAIN_CPP_PATH = "src/main.cpp"
SSID_PATTERN = r'#define WIFI_BROADCAST_SSID "GTIControl(\d+)"'

def find_pio():
    """Find PlatformIO CLI executable"""
    # Common locations for pio
    home = Path.home()
    possible_paths = [
        home / ".platformio/penv/bin/pio",
        home / ".platformio/penv/Scripts/pio.exe",
        home / ".local/bin/pio",
        Path("/usr/local/bin/pio"),
        Path("/opt/homebrew/bin/pio"),
    ]

    for path in possible_paths:
        if path.exists():
            return str(path)

    # Try system PATH as fallback
    return "pio"

PIO_PATH = find_pio()
print(f"[INFO] PlatformIO found at: {PIO_PATH}")

# Verify pio exists
if not Path(PIO_PATH).exists() and PIO_PATH != "pio":
    print(f"[ERROR] PlatformIO not found at {PIO_PATH}")
    print("Please install PlatformIO CLI: pip3 install platformio")
    sys.exit(1)

def get_current_ssid_number():
    """Read current SSID number from main.cpp"""
    with open(MAIN_CPP_PATH, 'r') as f:
        content = f.read()
    match = re.search(SSID_PATTERN, content)
    if match:
        return int(match.group(1))
    raise ValueError("Could not find WIFI_BROADCAST_SSID in main.cpp")

def set_ssid_number(number):
    """Update SSID number in main.cpp"""
    with open(MAIN_CPP_PATH, 'r') as f:
        content = f.read()
    new_content = re.sub(
        SSID_PATTERN,
        f'#define WIFI_BROADCAST_SSID "GTIControl{number}"',
        content
    )
    with open(MAIN_CPP_PATH, 'w') as f:
        f.write(new_content)
    print(f"[OK] SSID set to: GTIControl{number}")

def build_firmware():
    """Build firmware once"""
    print(f"\n[BUILD] Building firmware... (using: {PIO_PATH})")
    result = subprocess.run([PIO_PATH, "run"])
    if result.returncode != 0:
        print("[ERROR] Build failed!")
        sys.exit(1)
    print("[OK] Build successful\n")

def upload_firmware():
    """Upload firmware to connected device"""
    print("[UPLOAD] Uploading to device...")
    result = subprocess.run([PIO_PATH, "run", "-t", "upload"])
    if result.returncode != 0:
        print("[ERROR] Upload failed!")
        return False
    print("[OK] Upload successful\n")
    return True

def flash_single_device(device_number, ssid_number):
    """Flash a single device with specific SSID"""
    print(f"\n{'='*50}")
    print(f"DEVICE #{device_number} - SSID: GTIControl{ssid_number}")
    print(f"{'='*50}")

    set_ssid_number(ssid_number)
    build_firmware()

    if upload_firmware():
        print(f"[DONE] Device #{device_number} flashed with GTIControl{ssid_number}")
        return True
    return False

def main():
    os.chdir(os.path.dirname(os.path.abspath(__file__)))

    # Get device count from argument (default: 1)
    device_count = int(sys.argv[1]) if len(sys.argv) > 1 else 1

    # Get current SSID number
    current_number = get_current_ssid_number()
    print(f"Current SSID: GTIControl{current_number}")

    for i in range(device_count):
        next_number = current_number + i + 1

        if i > 0:
            print(f"\n{'*'*50}")
            input(f"Connect device #{i+1} and press ENTER to continue...")
            print(f"{'*'*50}")

        flash_single_device(i + 1, next_number)

    print(f"\n{'='*50}")
    print(f"COMPLETED: Flashed {device_count} device(s)")
    print(f"SSID range: GTIControl{current_number + 1} - GTIControl{current_number + device_count}")
    print(f"{'='*50}")

if __name__ == "__main__":
    main()
