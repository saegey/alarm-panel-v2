#!/usr/bin/env python3
"""
Simple MQTT harness for Wokwi simulation.

Publishes fake Home Assistant state topics and logs keypad command topics.
"""

import argparse
import json
import random
import signal
import sys
import time
from typing import Any

import paho.mqtt.client as mqtt


DEFAULT_BROKER = "broker.hivemq.com"
DEFAULT_PORT = 1883

TOPIC_DISPLAY_SET = "home/keypad/display/set"
TOPIC_DISPLAY_TIMEOUT = "home/keypad/display/timeout"
TOPIC_WEATHER = "weather/kbfi/state"
TOPIC_GARAGE_STATE = "garage/door/state"
TOPIC_GARAGE_COMMAND = "garage/door/command"
TOPIC_ALARMO_MASTER_STATE = "alarmo/state"
TOPIC_ALARMO_GARAGE_STATE = "alarmo/garage/state"
TOPIC_ALARMO_COMMAND = "alarmo/command"


def parse_args() -> argparse.Namespace:
  parser = argparse.ArgumentParser(description="Wokwi MQTT test harness")
  parser.add_argument("--host", default=DEFAULT_BROKER, help="MQTT broker host")
  parser.add_argument("--port", type=int, default=DEFAULT_PORT, help="MQTT broker port")
  parser.add_argument("--prefix", default="", help="Optional topic prefix (example: test/user1)")
  parser.add_argument("--interval", type=float, default=8.0, help="Weather publish interval seconds")
  return parser.parse_args()


def with_prefix(prefix: str, topic: str) -> str:
  if not prefix:
    return topic
  return f"{prefix.rstrip('/')}/{topic}"


def on_connect(client: mqtt.Client, _userdata: Any, _flags: dict[str, Any], rc: int) -> None:
  print(f"[HARNESS] Connected rc={rc}")
  client.subscribe(client.topic_garage_command)
  client.subscribe(client.topic_alarmo_command)
  client.subscribe(client.topic_display_set)
  client.subscribe(client.topic_status)
  print("[HARNESS] Subscribed to command/status topics")


def on_message(client: mqtt.Client, _userdata: Any, msg: mqtt.MQTTMessage) -> None:
  payload = msg.payload.decode("utf-8", errors="replace")
  print(f"[RX] {msg.topic} -> {payload}")

  if msg.topic == client.topic_garage_command and payload.strip().upper() == "TOGGLE":
    next_state = "open" if client.garage_state in ("closed", "closing") else "closed"
    client.garage_state = next_state
    client.publish(client.topic_garage_state, next_state, retain=True)
    print(f"[TX] {client.topic_garage_state} -> {next_state}")

  if msg.topic == client.topic_alarmo_command:
    try:
      data = json.loads(payload)
    except json.JSONDecodeError:
      return
    command = str(data.get("command", "")).lower()
    area = str(data.get("area", "")).lower()
    is_garage = area == "garage"
    state_topic = client.topic_alarmo_garage_state if is_garage else client.topic_alarmo_master_state

    command_to_state = {
        "arm_home": "armed_home",
        "arm_away": "armed_away",
        "arm_night": "armed_night",
        "arm_vacation": "armed_vacation",
        "disarm": "disarmed",
    }
    next_state = command_to_state.get(command)
    if next_state:
      client.publish(state_topic, next_state, retain=True)
      print(f"[TX] {state_topic} -> {next_state}")


def publish_initial_state(client: mqtt.Client) -> None:
  client.publish(client.topic_alarmo_master_state, "disarmed", retain=True)
  client.publish(client.topic_alarmo_garage_state, "disarmed", retain=True)
  client.publish(client.topic_garage_state, "closed", retain=True)
  client.publish(client.topic_display_timeout, "30", retain=True)
  client.publish(client.topic_display_set, "WAKE", retain=False)
  print("[HARNESS] Initial states published")


def publish_weather(client: mqtt.Client) -> None:
  temperature = round(random.uniform(45.0, 78.0), 1)
  condition = random.choice(["clear", "cloudy", "rainy", "windy"])
  payload = json.dumps({"temperature": temperature, "state": condition}, separators=(",", ":"))
  client.publish(client.topic_weather, payload, retain=False)
  print(f"[TX] {client.topic_weather} -> {payload}")


def main() -> int:
  args = parse_args()
  prefix = args.prefix.strip("/")

  client = mqtt.Client(client_id=f"wokwi-harness-{int(time.time())}", clean_session=True)
  client.on_connect = on_connect
  client.on_message = on_message

  client.topic_status = with_prefix(prefix, "home/keypad/status")
  client.topic_display_set = with_prefix(prefix, TOPIC_DISPLAY_SET)
  client.topic_display_timeout = with_prefix(prefix, TOPIC_DISPLAY_TIMEOUT)
  client.topic_weather = with_prefix(prefix, TOPIC_WEATHER)
  client.topic_garage_state = with_prefix(prefix, TOPIC_GARAGE_STATE)
  client.topic_garage_command = with_prefix(prefix, TOPIC_GARAGE_COMMAND)
  client.topic_alarmo_master_state = with_prefix(prefix, TOPIC_ALARMO_MASTER_STATE)
  client.topic_alarmo_garage_state = with_prefix(prefix, TOPIC_ALARMO_GARAGE_STATE)
  client.topic_alarmo_command = with_prefix(prefix, TOPIC_ALARMO_COMMAND)
  client.garage_state = "closed"

  print(f"[HARNESS] Connecting to {args.host}:{args.port}")
  client.connect(args.host, args.port, keepalive=60)
  client.loop_start()

  stop = False

  def handle_stop(_sig: int, _frame: Any) -> None:
    nonlocal stop
    stop = True

  signal.signal(signal.SIGINT, handle_stop)
  signal.signal(signal.SIGTERM, handle_stop)

  time.sleep(1.0)
  publish_initial_state(client)

  next_weather = 0.0
  while not stop:
    now = time.time()
    if now >= next_weather:
      publish_weather(client)
      next_weather = now + max(1.0, args.interval)
    time.sleep(0.1)

  print("[HARNESS] Stopping")
  client.loop_stop()
  client.disconnect()
  return 0


if __name__ == "__main__":
  sys.exit(main())
