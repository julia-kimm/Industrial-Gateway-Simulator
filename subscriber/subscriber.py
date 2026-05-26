import csv
import os
import sys
import json
import signal
import datetime
from pathlib import Path

import paho.mqtt.client as mqtt

BROKER_HOST = sys.argv[1] if len(sys.argv) > 1 else "localhost"
BROKER_PORT = int(sys.argv[2]) if len(sys.argv) > 2 else 1883
CSV_PATH = sys.argv[3] if len(sys.argv) > 3 else "../data/readings.csv"

DATA_TOPIC = "factory/line1/data"

# how many messages to receive before printing a report
REPORT_EVERY = 5


class Analyzer:
    def __init__(self, csv_path: str):
        self.csv_path = csv_path
        self.temps: list[int] = []
        self.humidities: list[int] = []
        self.count = 0
        self.invalid = 0  # failed to parse or missing fields

        # prepare CSV file for appending
        Path(os.path.dirname(csv_path) or ".").mkdir(parents=True, exist_ok=True)
        new_file = not os.path.exists(csv_path) or os.path.getsize(csv_path) == 0
        self._fh = open(csv_path, "a", newline="")
        self._writer = csv.writer(self._fh)
        if new_file:
            self._writer.writerow(["time", "temp", "humidity"])
            self._fh.flush()

    def add(self, temperature: int, humidity: int) -> None:
        now = datetime.datetime.now().strftime("%H:%M:%S")
        self._writer.writerow([now, temperature, humidity])
        self._fh.flush()

        self.temps.append(temperature)
        self.humidities.append(humidity)
        self.count += 1

    def mark_invalid(self) -> None:
        self.invalid += 1

    def report(self) -> str:
        lines = ["", "<REPORT>",
                 f"Total messages: {self.count}"]
        if self.temps:
            avg_t = sum(self.temps) / len(self.temps)
            avg_h = sum(self.humidities) / len(self.humidities)
            lines += [
                f"Average temp: {avg_t:.1f}",
                f"Max temp: {max(self.temps)}",
                f"Min temp: {min(self.temps)}",
                f"Average humidity: {avg_h:.1f}",
                f"Max humidity: {max(self.humidities)}",
                f"Min humidity: {min(self.humidities)}",
            ]
        else:
            lines.append("(no valid readings yet)")
        lines.append(f"Invalid readings: {self.invalid}")
        lines.append("==================")
        return "\n".join(lines)

    def close(self) -> None:
        self._fh.close()


def main() -> None:
    analyzer = Analyzer(CSV_PATH)

    #MQTT
    def on_connect(client, userdata, flags, reason_code, properties=None):
        if reason_code == 0:
            print(f"[SUBSCRIBER] connected to {BROKER_HOST}:{BROKER_PORT}")
            client.subscribe(DATA_TOPIC)
            print(f"[SUBSCRIBER] subscribed to '{DATA_TOPIC}'")
        else:
            print(f"[SUBSCRIBER][ERROR] connect failed rc={reason_code}")

    def on_message(client, userdata, msg):
        payload = msg.payload.decode("utf-8", errors="replace")
        try:
            data = json.loads(payload)
            temp = int(data["temperature"])
            humi = int(data["humidity"])
        except (ValueError, KeyError, json.JSONDecodeError):
            analyzer.mark_invalid()
            print(f"[SUBSCRIBER][ERROR] bad payload: {payload!r}")
            return

        analyzer.add(temp, humi)
        print(f"[SUBSCRIBER] saved  temp={temp}  humidity={humi}  "
              f"(total={analyzer.count})")

        if analyzer.count % REPORT_EVERY == 0:
            print(analyzer.report())

    # Client setup
    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id="python-subscriber")
    client.on_connect = on_connect
    client.on_message = on_message

    # Ctrl-C -> shutdown
    def shutdown(signum, frame):
        print("\n[SUBSCRIBER] stopping")
        print(analyzer.report())
        analyzer.close()
        client.disconnect()
        sys.exit(0)

    signal.signal(signal.SIGINT, shutdown)

    try:
        client.connect(BROKER_HOST, BROKER_PORT, 60)
    except Exception as exc:
        print(f"[SUBSCRIBER][ERROR] cannot reach broker: {exc}")
        analyzer.close()
        sys.exit(1)

    print("[SUBSCRIBER] waiting for messages (Ctrl-C to stop)")
    client.loop_forever()


if __name__ == "__main__":
    main()
