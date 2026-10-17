# Meta Muse Gadget for Custom ESP32-S3

A complete open-source port of the official **Meta Muse Gadget SDK** (`facebookincubator/muse-gadget-sdk`) running on a standard **ESP32-S3** development board equipped with:
- **Display**: 1.3" 240x240 ST7789 SPI LCD (with animated avatar & lip-sync)
- **Microphone**: INMP441 I2S Digital MEMS Microphone
- **Speaker**: MAX98357A I2S Class-D Amplifier + 4Ω/8Ω Speaker
- **Spoken Audio**: Integrated 100% Free Voice TTS (Zero API keys, zero subscription required)
- **Push-to-Talk**: BOOT Button (GPIO 0)

---

## 📁 Package Contents

```text
Meta-Muse-ESP32-S3/
├── README.md                      # This comprehensive guide
├── wiring/
│   └── WIRING_PINOUT.md           # Complete hardware pinout & power guide
├── firmware_binaries/             # Ready-to-flash binaries (No compiling needed!)
│   ├── bootloader.bin             # ESP32-S3 second-stage bootloader (0x0)
│   ├── partition-table.bin        # 16 MB partition table (0x10000)
│   ├── ota_data_initial.bin       # OTA boot selection image (0x17000)
│   ├── muse-gadget.bin            # Main application firmware (0x20000)
│   ├── flash.bat                  # 1-Click flasher script for Windows
│   └── flash.sh                   # 1-Click flasher script for Linux / macOS
└── source_code/                   # Source code & custom drivers
    ├── boards/
    │   ├── board_custom_s3_st7789.c   # Custom display & I2S audio driver
    │   └── Kconfig                    # Muse board Kconfig definitions
    ├── devices/
    │   └── sdkconfig.custom-s3-st7789 # Target sdkconfig (16MB Flash, 8MB PSRAM)
    ├── workflow/
    │   └── build-custom.yml           # GitHub Actions CI workflow for cloud build
    ├── muse_chat_session.cpp      # Voice chat session with embedded TTS engine
    ├── muse_settings.c            # NVS settings management
    └── muse_audio.c               # Audio pipeline & gain control
```

---

## 🔌 Hardware Wiring & Pinout

### 1. ST7789 1.3" SPI LCD (240x240)
| LCD Pin | ESP32-S3 GPIO | Notes |
| :--- | :--- | :--- |
| **SCL / CLK** | **GPIO 12** | SPI Clock (SPI Mode 3) |
| **SDA / MOSI**| **GPIO 11** | SPI Data Out |
| **RES / RST** | **GPIO 10** | Hardware Reset |
| **DC** | **GPIO 9** | Data / Command select |
| **BLK / BL** | **GPIO 7** | Backlight Control (3.3V Active High) |
| **CS** | *NC / GND* | Connect to GND or leave unconnected |
| **VCC** | **3.3V** | Power |
| **GND** | **GND** | Ground |

### 2. INMP441 I2S Microphone
| INMP441 Pin | ESP32-S3 GPIO | Notes |
| :--- | :--- | :--- |
| **SCK / BCLK**| **GPIO 6** | I2S Microphone Bit Clock (I2S1) |
| **WS / LRCL** | **GPIO 5** | I2S Word Select (32-bit slot width) |
| **SD / DOUT** | **GPIO 4** | I2S Serial Data |
| **L/R** | **GND** | Left channel selection |
| **VDD** | **3.3V** | Power |
| **GND** | **GND** | Ground |

### 3. MAX98357A I2S Audio Amplifier
| MAX98357A Pin | ESP32-S3 GPIO | Notes |
| :--- | :--- | :--- |
| **BCLK** | **GPIO 16** | I2S Speaker Bit Clock (I2S0) |
| **LRC / LRCLK**| **GPIO 17** | I2S Word Select (16-bit slot width) |
| **DIN** | **GPIO 15** | I2S Audio Data |
| **VIN** | **5V / VIN** | Connect to 5V rail for maximum volume |
| **GND** | **GND** | Ground |
| **GAIN** | *NC / GND* | Unconnected (12 dB) or GND (9 dB) |
| **SD_MODE** | *NC / 3.3V* | Leave unconnected for stereo mix |

### 4. Push-To-Talk Button
* Connect a momentary push-button between **GPIO 0** and **GND** (or use the onboard **BOOT** button).

---

## ⚡ How to Flash the Firmware (2 Easy Methods)

> [!NOTE]
> **No programming libraries, compilers, or ESP-IDF are required on your computer!** All binaries are already pre-compiled and ready in the `firmware_binaries/` folder.

---

### Method 1: 1-Click Flash Script (Standard)
Requires Python installed on your computer.

1. Open a terminal / command prompt and install `esptool`:
   ```bash
   pip install esptool
   ```
2. Connect your ESP32-S3 to your PC with a USB-C data cable.
3. Run the flasher script:
   - **On Windows:** Double-click **`firmware_binaries/flash.bat`** and enter your COM port (e.g. `COM12`).
   - **On Linux / macOS:** Run `./firmware_binaries/flash.sh /dev/ttyUSB0`.

---

### Method 2: 100% Zero-Install Web Flasher (Directly in Google Chrome / Edge)
You can flash directly from your web browser with **literally zero software installed**:

1. Open **Google Chrome** or **Microsoft Edge**.
2. Go to Espressif's official web flasher: **[https://espressif.github.io/esptool-js/](https://espressif.github.io/esptool-js/)**
3. Connect your ESP32-S3 via USB and click **Connect** (select your COM port in the pop-up).
4. Under **Flash Files**, add the 4 binary files from the `firmware_binaries/` folder with their corresponding flash offsets:
   * **`0x0`** $\to$ Choose **`bootloader.bin`**
   * **`0x10000`** $\to$ Choose **`partition-table.bin`**
   * **`0x17000`** $\to$ Choose **`ota_data_initial.bin`**
   * **`0x20000`** $\to$ Choose **`muse-gadget.bin`**
5. Click **Program** and wait ~30 seconds until the flash reaches 100%!

> [!IMPORTANT]
> When updating firmware later, do **NOT** erase `0x11000` (`nvs`) to preserve your pairing settings!

---

## 📱 Setting Up with the Meta Muse Mobile App

Once the firmware is flashed, you do not need any SDK tokens or complex commands. The device pairs seamlessly directly through the official Meta mobile app in **under 1 minute**:

### Step 1: Enable Developer Mode in the App
1. Open the official **Muse** app on your iPhone or Android phone.
2. Tap the **Settings** icon (gear icon in the top corner).
3. Tap **Devices**.
4. Scroll down to the bottom and turn **ON** the toggle for **`Developer mode`**.
   *(A **`+` (Add Device)** button will now appear in the top-right corner).*

### Step 2: Add Your Gadget
1. Tap the **`+`** (or **Add Device**) button.
2. The app will scan over Bluetooth LE and discover your device:
   ```text
   MuseGadget-XXXXXX
   ```
   *(e.g., `MuseGadget-AC3DDC`)*
3. Tap on your gadget in the list to connect.

### Step 3: Confirm Pairing on ESP32
1. The app will ask to confirm pairing with the physical device.
2. Press the **BOOT button (GPIO 0)** on your ESP32 once to confirm physical authorization.

### Step 4: Connect Wi-Fi
1. The app will immediately show a list of nearby Wi-Fi networks right on your phone screen.
2. Select your Wi-Fi network and enter your password directly inside the app.
3. Wait **~10 seconds** for setup to complete!

The animated Muse avatar will ignite on the ST7789 screen, and your gadget is ready! Hold the **BOOT button (GPIO 0)** to talk, and listen to the reply spoken aloud through the speaker!

---

## 🛠️ Building from Source

### Prerequisites:
- **ESP-IDF v6.0+** installed:
  ```bash
  git clone -b v6.0.1 --recursive https://github.com/espressif/esp-idf.git
  cd esp-idf && ./install.sh && . ./export.sh
  ```

### Build Command:
```bash
cd esp32
idf.py -B build -DIDF_TARGET="esp32s3" \
  -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;devices/sdkconfig.muse;devices/sdkconfig.custom-s3-st7789" \
  build
```

### Or using GitHub Actions:
Copy `source_code/workflow/build-custom.yml` into `.github/workflows/` of your GitHub repository. Every push will automatically compile the binaries in the cloud and let you download them from the **Actions** tab!

---

## 🔊 Audio & Display Features

1. **Clean Distortion-Free Audio**:
   - Volume limiter with headroom capped at $\pm 28000$ to prevent square-wave rail clipping on the MAX98357A.
   - Microphone gain set to **25 dB** with hardware DC-blocking high-pass filter.
   - Completely silent startup without any harsh test buzz tones.
2. **ST7789 Display Optimization**:
   - SPI Mode 3 with direct GPIO 7 backlight output.
   - 10ms hardware reset sequence ensuring 100% reliable initialization on every boot.
   - RGB color format alignment for crisp avatar rendering and live lip-sync animation.

---

## 📄 License & Credits
Based on the official **Meta Muse Gadget SDK** by Meta Platforms, Inc. Licensed under the **Apache License, Version 2.0**.
Custom hardware drivers and voice TTS integration provided for community open-source use.
