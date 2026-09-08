ESP-NOW Based Walkie Talkies
By Fabio Salgado

Overview
This project implements a pair of battery-powered digital walkie talkies using the ESP32-S3-WROOM-1U microcontroller. Communication is achieved using the ESP-NOW protocol over the 2.4 GHz band. The system allows users to transmit voice wirelessly without requiring Wi-Fi infrastructure or cellular connectivity.
Each device is capable of half-duplex communication using a push-to-talk interface. In testing, the system achieved a reliable range of approximately 50–100 meters indoors and up to 500 meters outdoors under optimal conditions with an external antenna. Additional features include adjustable volume control and an on-device user interface displaying system information.

Components and Their Purpose
User Interface:
The user interface consists of a 0.96-inch I2C OLED display, a 5-way navigation switch, and a push button. The OLED display provides real-time feedback, including volume level and the device’s MAC address. The navigation switch allows users to scroll through menu options such as volume control, device address, and system information. The push button is used for push-to-talk functionality, enabling voice transmission only when pressed.

Audio Input and Output:
Audio capture is handled by the INMP441, a digital microphone that outputs audio data via the I2S protocol. This eliminates the need for an external analog-to-digital converter. A 10 kΩ potentiometer is used to control volume by adjusting the gain applied to the audio signal in software.

After processing, the digital audio is sent to an amplifier, which increases the signal strength to drive a speaker. This allows the received audio to be played at a usable volume level.

Communication System:
Wireless transmission is handled by the ESP32 itself using the ESP-NOW protocol. The WROOM-1U variant includes a U.FL connector for an external 2.4 GHz antenna, which significantly improves signal reliability and range. Without an external antenna, communication becomes unreliable beyond short distances.

Power System:
Each device is powered by a 2500 mAh lithium polymer battery. A Adafruit PowerBoost 1000C is used to provide charging functionality and voltage regulation. The system also includes a 1000 µF capacitor to stabilize voltage during transmission and reduce noise from current spikes. A physical switch allows the user to disconnect power from the ESP32 when the device is not in use.

Sources of Difficulty
Two major challenges were encountered: audio quality and power regulation.

Initially, audio transmission was functional but of poor quality. Various digital signal processing techniques, including filtering and compression, were implemented to improve clarity. However, the root cause was ultimately traced to a hardware issue: an improperly soldered ground connection on the ESP32. Once corrected, audio quality improved significantly.

The second issue involved inconsistent voltage output from the PowerBoost module. While the regulator produced a stable 5 V output under no load, it dropped to approximately 3.2 V when connected to the full circuit. This voltage was insufficient to power the system reliably.

As a workaround, the regulated 5 V output was bypassed, and the system was powered directly from the battery via the BAT pin. While this allowed the system to function at higher battery charge levels, it introduced variability in supply voltage and reduced overall efficiency.

Comparison to Real-World Systems
This system shares several similarities with commercial walkie talkies. Both are half-duplex communication devices, utilize antennas, and enable direct communication without external infrastructure.

However, the primary difference lies in the communication method. Traditional walkie talkies typically use analog FM or standardized digital radio protocols, which are optimized for long-range and reliable voice transmission. In contrast, this system uses ESP-NOW, a packet-based wireless protocol designed for short-range data communication. As a result, the ESP32-based system has reduced range and reliability but offers greater flexibility and lower cost.

Future Improvements

There are two primary areas for improvement. First, the device would benefit from a fully enclosed casing to protect internal components and improve durability. A custom-designed enclosure would also enhance usability and portability.

Second, the power system could be redesigned to provide a stable regulated voltage. This could involve selecting a different power management module or redesigning the circuit to ensure proper operation of the existing regulator. A stable power supply would improve efficiency, reliability, and battery longevity.

Libraries Used
- Arduino ESP32 Core (WiFi, ESP-NOW, Wire): https://github.com/espressif/arduino-esp32
- Adafruit GFX: https://github.com/adafruit/Adafruit-GFX-Library
- Adafruit SSD1306: https://github.com/adafruit/Adafruit_SSD1306
