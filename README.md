# 🌱 Smart Plant Care System

An ESP32-S3-based smart plant care system that continuously monitors soil moisture, air temperature, and humidity, and automatically makes irrigation decisions based on real sensor data. The current implementation uses test parameters for validating the watering logic before final real-world calibration.

Built with PlatformIO (Arduino framework) and FreeRTOS, with an emphasis on power-conscious sensor design and safe hardware practices.

## Features

**Implemented**

* ✅ Soil moisture sensing (resistive) with power-gating to extend sensor lifespan
* ✅ Air temperature and humidity monitoring (DHT22)
* ✅ Unified FreeRTOS sensor task with mutex-protected Serial logging
* ✅ Automatic watering decision logic with configurable moisture threshold
* ✅ Cooldown protection to prevent repeated watering
* ✅ Configurable watering duration and decision interval
* ✅ Relay control (control side tested independently)
* ✅ Persistent moisture threshold via NVS (survives reboot)

**Planned**

* ⏳ Pump + external power circuit integration
* ⏳ Web dashboard for real-time monitoring and device control
* ⏳ Final physical assembly and real-world calibration

See [Project Roadmap](#project-roadmap) below for the full phase breakdown.

## Hardware

| Component                                    | Purpose                                                |
| --------------------------------------------- | ------------------------------------------------------ |
| ESP32-S3-WROOM-1                             | Main controller                                        |
| Resistive soil moisture sensor (LM393-based) | Soil moisture reading                                  |
| DHT22                                        | Air temperature and humidity                           |
| 5V single-channel relay module               | Pump switching (control side wired; load side pending) |
| Miniature submersible water pump             | Irrigation actuator *(not yet wired)*                  |
| 1N4007 diode                                 | Flyback protection for pump *(not yet wired)*          |
| External 5V/12V adapter                      | Independent power for the pump *(not yet wired)*       |

## Wiring

![Wiring Diagram](docs/wiring-diagram.svg)

A real photo of the current physical build is available in [`docs/photos/`](docs/photos/).

### Why the soil sensor is power-gated

The resistive soil sensor is powered through a GPIO pin (not tied directly to 3.3V) and is only switched on for the brief moment of each reading. Continuous power across the probes accelerates electrochemical corrosion in moist soil, so gating the power — combined with a long read interval in production — significantly extends the sensor's usable life.

### Why the watering cooldown resets on every reboot

The moisture threshold is persisted in NVS and survives reboots. The last-watering timestamp is intentionally **not** persisted yet: it's based on `millis()`, which resets to zero on every boot, so storing and comparing it across reboots could produce an invalid (underflowed) duration and trigger an unsafe, immediate watering cycle. Until real time (NTP) is available in Phase 8, the system takes the conservative approach of treating every boot as "just watered," requiring a full cooldown period to pass before the first watering cycle after a restart.

## Software Architecture

The project runs on FreeRTOS (via the Arduino core):

* **`environmentTask`** — handles soil moisture and DHT22 measurements in a unified sensing cycle.
* **`relayControlTask`** — evaluates watering conditions, enforces cooldown protection, and controls the relay for automatic irrigation.
* **`serialMutex`** — ensures thread-safe Serial logging across FreeRTOS tasks.
* **NVS (`Preferences`)** — persists the configurable moisture threshold across reboots.

## Getting Started

```bash
git clone https://github.com/USERNAME/REPO-NAME.git
cd REPO-NAME
pio run --target upload
pio device monitor
```

## Project Roadmap

* [x] Phase 1 — Soil moisture sensor + calibration
* [x] Phase 2 — DHT22 integration
* [x] Phase 3 — Unified sensor task
* [x] Phase 4 — Relay control test *(hardware issue under investigation, see below)*
* [x] Phase 5 — Automatic watering decision logic *(implemented with test parameters)*
* [x] Phase 6 — Persistent moisture threshold via NVS
* [ ] Phase 7 — Pump and power circuit integration
* [ ] Phase 8 — Web dashboard for monitoring and control
* [ ] Phase 9 — Final calibration and assembly

## Known Issues

* **Relay module shows abnormal voltage drop under load.** With a fully direct wiring path from the ESP32's 5V pin to the relay's VCC/GND (bypassing the breadboard entirely), voltage still drops from ~4.5V to ~1V once the relay coil draws current. Jumper wires, connectors, and the ESP32 power pin have all been ruled out through direct multimeter testing. The relay module itself is currently suspected to be faulty and is pending replacement/confirmation.

## License

MIT