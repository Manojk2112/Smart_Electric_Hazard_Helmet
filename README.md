# Smart_Electric_Hazard_Helmet
An IoT-powered smart safety helmet built on the Seeed Studio XIAO ESP32-C6 featuring non-contact AC voltage hazard detection, wear-state proximity monitoring, and automated cellular emergency alerts.

An advanced, microcontroller-based IoT safety wearable designed to protect electrical workers by proactively detecting live AC voltage hazards, verifying proper helmet usage via proximity sensing, and instantly triggering emergency notifications.

## ✨ Key Features
* **Non-Contact AC Voltage Hazard Detection:** Continuously monitors for high-voltage proximity using a calibrated analog sensor setup with smooth filtering and threshold intelligence.
* **Smart Wear-State Verification:** Integrates a VCNL4040 proximity sensor to ensure safety alerts only trigger when the helmet is actively being worn.
* **Automated Cellular Emergency Alerts:** Equipped with a SIM800L GSM module to send immediate SMS messages when a live voltage hazard is detected.
* **Low-Power & Compact Architecture:** Powered by the Seeed Studio XIAO ESP32-C6, offering robust Wi-Fi/BLE and low-power capabilities.

---

## 📌 Master Pin Map (XIAO ESP32-C6)

| Pin  |  $GPIO  |        unction / Component       |
| :--- | :------ | :------------------------------- |
|  D0  | GPIO 0  | AC Voltage Sensor (Analog Input) |
|  D1  | GPIO 1  |     SIM800L Module (UART TX)     |
|  D2  | GPIO 2  |     SIM800L Module (UART RX)     |
|  D3  | GPIO 3  | Haptic Motor / Buzzer (Digital Output) |
|  D4  | GPIO 22 | VCNL4040 Proximity Sensor (I2C SDA) |
|  D5  | GPIO 23 | VCNL4040 Proximity Sensor (I2C SCL) |
|  D6  | GPIO 16 | VCNL4040 Proximity Sensor (Interrupt / INT) |
|  D7  | GPIO 7  | Touch Sensor (Digital Input) |
|  D8  | GPIO 19 | MAX98357A Audio Amplifier (I2S LRC / WS) |
|  D9  | GPIO 20 | MAX98357A Audio Amplifier (I2S BCLK) |
|  D10 | GPIO 18 | MAX98357A Audio Amplifier (I2S DIN) |

---

## 🛠️ Tech Stack & Hardware
* **Microcontroller:** Seeed Studio XIAO ESP32-C6
* **Sensors:** Custom AC Voltage Sniffer, VCNL4040 Proximity/Ambient Light Sensor
* **Communication:** SIM800L GSM/GPRS Module
* **Audio / Feedback:** MAX98357A I2S Amplifier, Haptic Motor
* **IDE / Framework:** Arduino IDE / PlatformIO
