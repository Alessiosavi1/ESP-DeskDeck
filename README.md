# ESP-DeskDeck
ESP-DeskDeck is a compact, DIY alternative to commercial macro-pads. Built around an ESP32-C3 Mini, it serves as both an ambient information display and a wireless 4-key macro pad. It fetches real-time weather data for a configured city and shows it on an OLED screen, while also sending commands over your local Wi-Fi to a host PC.

The device is fully configurable through a modern web interface hosted directly on the ESP32.

## Star History

<a href="https://www.star-history.com/?repos=Alessiosavi1%2FESP-DeskDeck&type=date&legend=top-left">
 <picture>
   <source media="(prefers-color-scheme: dark)" srcset="https://api.star-history.com/chart?repos=Alessiosavi1/ESP-DeskDeck&type=date&theme=dark&legend=top-left" />
   <source media="(prefers-color-scheme: light)" srcset="https://api.star-history.com/chart?repos=Alessiosavi1/ESP-DeskDeck&type=date&legend=top-left" />
   <img alt="Star History Chart" src="https://api.star-history.com/chart?repos=Alessiosavi1/ESP-DeskDeck&type=date&legend=top-left" />
 </picture>
</a>

## Features

- **Ambient Information Display**:
    - Fetches and displays real-time weather data (temperature, conditions, wind, humidity, visibility) from the free [Open-Meteo API](https://open-meteo.com/).
    - Can toggle between displaying weather and the current time on a 128x32 OLED screen.
- **Wireless Macro Pad**:
    - Features four physical buttons that can be configured to perform various actions on your PC.
    - Communicates with the host computer over Wi-Fi, eliminating the need for a USB cable.
- **Web-Based Configuration**:
    - Hosts a self-contained web server for configuration.
    - A responsive weather dashboard to view current conditions, featuring animated backgrounds that match the weather (sun, rain, clouds, snow, etc.).
    - An intuitive "Configura Tasti" page to assign different actions to each button.
- **PC Control Script**:
    - A companion Python script that automatically discovers the ESP-DeskDeck on your local network.
    - Listens for button press events and executes the configured commands.
    - Supports media controls, keyboard shortcuts, and opening websites.

## Hardware Components

*   ESP32-C3 Mini Board (or a similar ESP32 board, pins may need adjustment)
*   SSD1306 128x32 I2C OLED Display
*   4x Tactile Push Buttons
*   Breadboard and Dupont Wires

## Setup and Installation

The setup involves two main parts: flashing the firmware to the ESP32 and running the control script on your PC.

### 1. ESP32 Firmware

#### Prerequisites
- [Arduino IDE](https://www.arduino.cc/en/software)
- ESP32 Board Support for Arduino IDE
- The following Arduino libraries installed via the Library Manager:
    - `WebServer`
    - `HTTPClient`
    - `ArduinoJson`
    - `Adafruit GFX Library`
    - `Adafruit SSD1306`

#### Wiring
Connect the components to your ESP32 board according to the diagram below:

| Component             | ESP32 Pin |
| --------------------- | --------- |
| OLED Display SDA      | GPIO 8    |
| OLED Display SCL      | GPIO 9    |
| Button 1              | GPIO 0    |
| Button 2              | GPIO 1    |
| Button 3              | GPIO 2    |
| Button 4              | GPIO 3    |

*Note: The buttons should be wired to connect the GPIO pin to GND when pressed.*

#### Flashing
1.  Open the `ESP32_Meteo_C3_IP_Version9.ino` file in the Arduino IDE.
2.  Modify the Wi-Fi credentials at the top of the file to match your network:
    ```cpp
    const char* ssid_casa = "YOUR_WIFI_SSID";
    const char* pass_casa = "YOUR_WIFI_PASSWORD";
    ```
3.  Select your ESP32-C3 board from the `Tools > Board` menu.
4.  Select the correct COM port.
5.  Upload the sketch to your board. On first boot, the device will connect to your Wi-Fi and display its IP address on the OLED screen.

### 2. PC Control Script

The Python script listens for commands from the ESP32 and executes them on your computer.

#### Prerequisites
- Python 3.x
- The following Python packages:
  ```sh
  pip install requests pyautogui keyboard
  ```
  *Note: The `keyboard` library may require administrator/root privileges to function correctly on some operating systems.*

#### Configuration
1.  Open the `Esp controll.py` file.
2.  Adjust the `IP_PREFIX` variable to match your local network's subnet. For example, if your computer's IP is `192.168.1.50`, change the prefix to `"192.168.1."`.
    ```python
    # ---------------- CONFIG ----------------
    IP_PREFIX = "192.168.1."
    # ...
    # ----------------------------------------
    ```
3.  Run the script from your terminal:
    ```sh
    python "Esp controll.py"
    ```
    The script will start scanning your network for the ESP-DeskDeck. Once found, it will print the device's IP and begin listening for button presses.

## Usage

1.  **Power On**: Once the ESP32 is powered on and the firmware is flashed, it will connect to your Wi-Fi and display its IP address. If it fails to connect, it will create an Access Point named `ESP32-Meteo-Config` (password: `12345678`) and its IP will be `192.168.4.1`.
2.  **Access Web Interface**: Open a web browser and navigate to the IP address shown on the OLED display.
3.  **Set Location**: On the main page, type a city name into the search bar and click "Cerca". The device will fetch and display weather for that location.
4.  **Configure Buttons**: Navigate to the `/tasti` page (e.g., `http://192.168.1.123/tasti`). Here you can assign an action to each of the four buttons.
    - Click on a button's configuration card to open the action picker.
    - Select an action and click "Salva" (Save).
    - If you see a green pulse on the card when you physically press a button, it means the device is correctly registering the press.

#### Available Actions
- `display_toggle`: Toggles the OLED screen between weather and time view.
- `media_play`: Play/Pause media.
- `media_next`: Next track.
- `media_prev`: Previous track.
- `media_vol_up`: Volume up.
- `media_vol_down`: Volume down.
- `media_mute`: Mute volume.
- `ctrl_c`: Copy (Ctrl+C).
- `ctrl_v`: Paste (Ctrl+V).
- `ctrl_z`: Undo (Ctrl+Z).
- `win_d`: Show Desktop (Win+D).
- `screenshot`: Take a screenshot (Win+Shift+S).
- `open_site`: Opens a specific URL (requires setting the URL in the web UI).

5.  **Run the System**: With the `Esp controll.py` script running on your PC, you can now press the physical buttons on the DeskDeck to trigger the configured actions.

## License

This project is licensed under the **Copyright**. See the [LICENSE](LICENSE) file for more details.
