ESP32 Embedded Brew

A Wi-Fi–connected embedded controller for an off-the-shelf drip coffee maker.
Features responsive web UI, brewing automation, onboard sensor telemetry, and OTA firmware updates.

⸻

Overview

Embedded Brew replaces the physical brew button with an ESP32-C3-controlled relay and exposes a lightweight control panel at:

http://brew.local/

The system includes:
	•	Web UI with temperature/pressure gauges, mug animation, and brew controls
	•	Wi-Fi STA mode → fallback SoftAP mode (smart_coffee)
	•	mDNS discovery (brew.local)
	•	OTA firmware updates powered by ElegantOTA
	•	BMP280 sensor for temp/pressure telemetry
	•	Safety auto-off logic (UI resets 40 minutes after brew start)
	•	OLED disabled for embedded use (powered down at boot)

This project is designed to be installed inside a coffee maker enclosure and treated as a “sealed appliance brain.”

⸻

Hardware

Board: ESP32-C3 Dev Module
Relay: GPIO 2 → momentary press simulation
Sensor: BMP280 (I2C @ 0x76/0x77)
OLED: SSD1306 72×40 (powered off via U8g2 sleep mode)

BREW button is simulated by closing GPIO2 → GND for ~250 ms.

⸻

Networking

Device attempts:
	1.	Home Wi-Fi (STA mode) → SSID/password hardcoded
	2.	If STA fails after 60 seconds →
SoftAP mode
	•	SSID: smart_coffee
	•	Pass: 11111111

mDNS is always enabled when STA mode succeeds:

brew.local


⸻

Web Endpoints

Endpoint	Description
/	Main control panel (HTML UI)
/press	Momentary brew-button simulation
/metrics	JSON uptime + sensor data for JS polling
/update	ElegantOTA firmware upload interface


⸻

Firmware Build

Project structure:

esp32_embedded_brew/
│
├── src/
│   └── main.cpp
└── README.md

To build/upload (Arduino CLI or PlatformIO):

pio run
pio run --target upload

Or use Arduino IDE with ESP32-C3 board support installed.

⸻

Contributing

Pull requests welcome. The goal is a compact, reliable embedded controller for appliance retrofits—keep PRs focused and minimal.

⸻

License

MIT License.