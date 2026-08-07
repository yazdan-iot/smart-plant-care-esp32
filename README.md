# 🌱 Smart Plant Care System

**Status: ✅ Complete**

An ESP32-S3-based smart plant care system that continuously monitors soil moisture, air temperature, and humidity, automatically makes irrigation decisions based on real sensor data, and exposes a live web dashboard for monitoring and control. All phases — sensing, decision logic, relay/pump actuation, and the web dashboard — are implemented and wired end to end.

Built with PlatformIO (Arduino framework) and FreeRTOS, with an emphasis on power-conscious sensor design and safe hardware practices.

## Features

* ✅ Soil moisture sensing (resistive) with power-gating to extend sensor lifespan
* ✅ Air temperature and humidity monitoring (DHT22)
* ✅ Unified FreeRTOS sensor task with mutex-protected Serial logging
* ✅ Automatic watering decision logic with configurable moisture threshold
* ✅ Cooldown protection to prevent repeated watering
* ✅ Configurable watering duration and decision interval
* ✅ Relay control, powered from an independent external 5V rail (see [Known Issues](#known-issues))
* ✅ Pump wired and actuated through the relay (COM/NO), powered independently of the ESP32
* ✅ Persistent moisture threshold via NVS (survives reboot)
* ✅ Live web dashboard served directly from the device (WiFi + LittleFS)
* ✅ REST API for live status, remote configuration, and manual watering override

See [Project Roadmap](#project-roadmap) below for the full phase breakdown.

## Hardware

| Component                                    | Purpose                                                |
| --------------------------------------------- | ------------------------------------------------------ |
| ESP32-S3-WROOM-1                             | Main controller                                        |
| Resistive soil moisture sensor (LM393-based) | Soil moisture reading                                  |
| DHT22                                        | Air temperature and humidity                           |
| 5V single-channel relay module               | Pump switching — powered from the MB102 rail, not the ESP32 5V pin (see [Known Issues](#known-issues)) |
| Miniature submersible water pump             | Irrigation actuator, switched via relay COM/NO          |
| MB102 breadboard power supply module         | Provides an independent, regulated 5V rail for the relay coil and pump, isolated from the ESP32's own 5V pin |
| 1N4007 diode                                 | Flyback protection for the pump's inductive load        |

## Wiring

![Wiring Diagram](docs/wiring-diagram.svg)

A real photo of the current physical build is available in [`docs/photos/`](docs/photos/).

**Relay (control side):** ESP32 GPIO15 → relay IN · MB102 5V → relay VCC · MB102 GND → relay GND · MB102 GND → ESP32 GND (common ground).

**Pump (load side):** MB102 5V (+) → relay COM · relay NO → pump (+) · pump (−) → MB102 GND (−) directly.

### Why the soil sensor is power-gated

The resistive soil sensor is powered through a GPIO pin (not tied directly to 3.3V) and is only switched on for the brief moment of each reading. Continuous power across the probes accelerates electrochemical corrosion in moist soil, so gating the power — combined with a long read interval in production — significantly extends the sensor's usable life.

### Why the relay and pump are powered from the MB102, not the ESP32 5V pin

The ESP32-S3's 5V pin is not an independent, high-current power source — it is effectively a pass-through from the USB VBUS line (through a protection diode, and on some boards a polyfuse). It's meant to supply a few tens of milliamps at most, not external loads like a relay coil or pump, which draw well beyond that. Under load, the combined voltage drop across the protection diode, the USB cable/port's own current limit, and the board's internal trace resistance is large enough to collapse the rail (observed dropping from ~4.5V to ~1.1V under load — see [Known Issues](#known-issues)).

Because of this, the relay coil and the pump are both powered from the **MB102 module's regulated 5V rail**, with only the relay's control (IN) pin driven by an ESP32 GPIO, and a **common ground** shared between the ESP32 and the MB102.

### Why the watering cooldown resets on every reboot

The moisture threshold is persisted in NVS and survives reboots. The last-watering timestamp is intentionally **not** persisted: it's based on `millis()`, which resets to zero on every boot, so storing and comparing it across reboots could produce an invalid (underflowed) duration and trigger an unsafe, immediate watering cycle. Until real time (NTP) is available, the system takes the conservative approach of treating every boot as "just watered," requiring a full cooldown period to pass before the first watering cycle after a restart.

## Web Dashboard

Once connected to WiFi, the device serves a live dashboard directly from its own flash storage (LittleFS) — no external server or cloud service required.

1. Open the Serial Monitor after boot and note the printed IP address
2. Visit `http://<device-ip>` from any browser on the same network
3. The dashboard polls the device every few seconds for live readings, and falls back to a clearly labeled demo mode if the device becomes unreachable

**API endpoints:**

| Method | Path | Purpose |
|---|---|---|
| GET | `/api/status` | Live sensor readings and system state |
| POST | `/api/settings` | Update moisture threshold, cooldown, watering duration, decision interval |
| POST | `/api/water` | Trigger a manual watering cycle override |

## Software Architecture

The project runs on FreeRTOS (via the Arduino core):

* **`environmentTask`** — handles soil moisture and DHT22 measurements in a unified sensing cycle.
* **`relayControlTask`** — evaluates watering conditions, enforces cooldown protection, and controls the relay for automatic (and manually triggered) irrigation.
* **`webServerTask`** — serves the dashboard and REST API over WiFi.
* **`serialMutex`** — ensures thread-safe Serial logging across FreeRTOS tasks.
* **NVS (`Preferences`)** — persists the configurable moisture threshold across reboots.
* **LittleFS** — stores the dashboard's static HTML file on the device's flash.

## Getting Started

```bash
git clone https://github.com/USERNAME/REPO-NAME.git
cd REPO-NAME
pio run --target upload
pio run --target uploadfs
pio device monitor
```

> Both the firmware and the filesystem image need to be uploaded separately — `uploadfs` pushes the dashboard HTML in `data/` to the device's flash.

## Project Roadmap

* [x] Phase 1 — Soil moisture sensor + calibration
* [x] Phase 2 — DHT22 integration
* [x] Phase 3 — Unified sensor task
* [x] Phase 4 — Relay control test *(root cause of voltage drop identified — see Known Issues; relay confirmed working with MB102 supply)*
* [x] Phase 5 — Automatic watering decision logic *(implemented with test parameters)*
* [x] Phase 6 — Persistent moisture threshold via NVS
* [x] Phase 7 — Pump and power circuit integration *(pump wired via relay COM/NO, powered from MB102)*
* [x] Phase 8 — Web dashboard for monitoring and control
* [x] Phase 9 — Final assembly

## Known Issues

* **~~Relay module shows abnormal voltage drop under load~~ — Resolved: root cause identified, not a faulty relay.** Even with a fully direct wiring path from the ESP32's 5V pin to the relay's VCC/GND (bypassing the breadboard entirely), voltage dropped from ~4.5V to ~1.1V once the relay coil drew current. Jumper wires, connectors, and the ESP32 power pin's continuity had all been ruled out through direct multimeter testing.

  Follow-up testing confirmed the relay itself is not faulty: powering it from a **separate, dedicated 5V supply** eliminated the voltage drop entirely. The root cause is that the ESP32-S3's 5V pin is only a pass-through from USB VBUS (through a protection diode and limited cable/port current capacity), not a regulated high-current rail — it cannot supply the current a relay coil (or pump) draws without collapsing.

  **Resolution:** the relay coil and the pump are both powered from the MB102 module's 5V rail, never from the ESP32's own 5V pin, with a common ground between the ESP32 and the MB102. This is now reflected in the [Wiring](#wiring) section.

## License

MIT
