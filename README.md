# Embedded Gateway Simulator (C++, Python, Linux, MQTT)

A simulator that reproduces the typical data flow of an industrial IoT gateway.

A sensor generates data, the gateway validates and logs it before transmitting MQTT, and a Python subscriber stores, analyzes, and reports the data.

> It uses a real Mosquitto broker, limosquitto(C++), and paho-mqtt (Python) - a genuine MQTT stack, not faked communication

---

## Overview — The Reason why I built this

In real industrial settings, a gateway does not merely forward data produced by sensors and PLCs.

It validates whether values are normal, logs any problems, and forwards data to the system that neet it via network protocols.

This project implements those core responsibilities at small scale, aiming to demonstrate the following skills at once.


- **C/C++**: sensor simulator and gateway logic, parsing/validation, error handling
- **Python**: MQTT reception, CSV storage, statistical analysis and reporting
- **Linux**: terminal build/run, log monitoring, process management
- **Networking / MQTT**: publish/subscribe, topic structure, broker integration

---

## Architecture

데이터 흐름:

1. **Sensor** generates temperature/humidity every 3 seconds(occasionally injecting anomalies). It appends a `temp,humidity` line to a file and also prints to stdout. .
2. **Gateway** follows the file like `tail -f`, parses new lines, and checks the valid range(0-100). It records every event to `gateway.log` and publishes only valid values over MQTT.
3. **Broker** relayes messages on the `factory/line1/#` topics.
4. **Subscriber** subscribes to `factory/line1/data`stores it to CSV, and prints averages/max/min/message count periodically and on shutdown.

---

## Features

- **Sensor**: random temp/humidity generation, intentional anomaly injection (for validation testing), file +stdout output
- **Gateway**: line parsing, range validation, structured `[INFO]`/`[ERROR]` logging, MQTT publish via libmosquitto, error handling for broker-connection failures and anomaly drops
- **Subscriber**: paho-mqtt subscription, CSV storage, avg/max/min/count analysis, periodic reports
- **Topics**:
  - `factory/line1/temperature` — single temperature value
  - `factory/line1/humidity` — single humidity value
  - `factory/line1/data` — `{"temperature":..,"humidity":..}` JSON

---
