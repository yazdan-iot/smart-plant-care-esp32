# 🌱 Smart Plant Care System

An ESP32-S3-based smart plant care system that continuously monitors soil moisture, air temperature, and humidity, and will eventually automate irrigation based on real sensor data — not a fixed timer.

Built with PlatformIO (Arduino framework) and FreeRTOS, with an emphasis on power-conscious sensor design and safe hardware practices.

## Features

**Implemented**

* ✅ Soil moisture sensing (resistive) with power-gating to extend sensor lifespan
* ✅ Air temperature and humidity monitoring (DHT22)
* ✅ Unified FreeRTOS sensor task with mutex-protected Serial logging
* ✅ Relay control (control side tested independently)

**Planned**

* ⏳ Automated watering logic with overwatering protection
* ⏳ Persistent configuration and state via NVS
* ⏳ Pump + external power circuit integration
* ⏳ Web dashboard for real-time monitoring and device control
* ⏳ Final physical assembly and real-world calibration

See [Project Roadmap](#project-roadmap) below for the full phase breakdown.

## Hardware

| Component                                    | Purpose                                                |
| -------------------------------------------- | ------------------------------------------------------ |
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

## Software Architecture

The project runs on FreeRTOS (via the Arduino core):

* **`environmentTask`** — a single task that handles both the soil moisture and DHT22 readings in one unified cycle, printing a combined status line
* **`relayControlTask`** — currently a standalone test task for validating relay control independent of sensor logic
* **`serialMutex`** — a shared mutex ensuring Serial output from different tasks never interleaves

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
* [ ] Phase 5 — Watering decision logic
* [ ] Phase 6 — Persistent state via NVS
* [ ] Phase 7 — Pump and power circuit integration
* [ ] Phase 8 — Web dashboard for monitoring and control
* [ ] Phase 9 — Final calibration and assembly

## Known Issues

* **Relay module shows abnormal voltage drop under load.** With a fully direct wiring path from the ESP32's 5V pin to the relay's VCC/GND (bypassing the breadboard entirely), voltage still drops from ~4.5V to ~1V once the relay coil draws current. Jumper wires, connectors, and the ESP32 power pin have all been ruled out through direct multimeter testing. The relay module itself is currently suspected to be faulty and is pending replacement/confirmation.

## License

MIT
