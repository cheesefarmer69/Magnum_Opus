"""Dunne MQTT-laag bovenop paho-mqtt 2.x (CallbackAPIVersion.VERSION2).

Houdt het laatste bericht per topic bij + een wachtrij per topic, en biedt
`wacht_op(topic, predicate, timeout)` voor synchroon test-werk. JSON wordt
automatisch gedecodeerd; lukt dat niet, dan blijft de payload de ruwe string.
"""
from __future__ import annotations

import json
import threading
import time
from collections import defaultdict, deque
from typing import Any, Callable

import paho.mqtt.client as mqtt


class MqttBus:
    def __init__(self, broker: str, port: int = 1883, client_id: str | None = None):
        self.broker = broker
        self.port = port
        self._client = mqtt.Client(
            mqtt.CallbackAPIVersion.VERSION2,
            client_id=client_id or f"speltest-{int(time.time())}",
        )
        self._client.on_connect = self._on_connect
        self._client.on_message = self._on_message
        self._lock = threading.Lock()
        self._laatste: dict[str, Any] = {}
        self._wachtrij: dict[str, deque] = defaultdict(lambda: deque(maxlen=200))
        self._abonnementen: list[str] = []
        self._event = threading.Event()  # geseind bij elk nieuw bericht
        self.verbonden = threading.Event()

    # ---- lifecycle ----
    def connect(self, timeout: float = 10.0) -> None:
        self._client.connect(self.broker, self.port, keepalive=30)
        self._client.loop_start()
        if not self.verbonden.wait(timeout):
            raise TimeoutError(f"Geen MQTT-verbinding met {self.broker}:{self.port}")

    def close(self) -> None:
        try:
            self._client.loop_stop()
            self._client.disconnect()
        except Exception:
            pass

    def subscribe(self, topics: list[str], qos: int = 0) -> None:
        for t in topics:
            self._client.subscribe(t, qos)
            if t not in self._abonnementen:
                self._abonnementen.append(t)

    # ---- publish ----
    def publish_json(self, topic: str, payload: Any, qos: int = 0, retain: bool = False) -> None:
        self._client.publish(topic, json.dumps(payload), qos=qos, retain=retain)

    def publish_str(self, topic: str, payload: str, qos: int = 0, retain: bool = False) -> None:
        self._client.publish(topic, payload, qos=qos, retain=retain)

    def publish_raw(self, topic: str, payload: bytes | str, qos: int = 0, retain: bool = False) -> None:
        self._client.publish(topic, payload, qos=qos, retain=retain)

    # ---- lezen ----
    def laatste(self, topic: str) -> Any:
        with self._lock:
            return self._laatste.get(topic)

    def wis(self, topic: str) -> None:
        with self._lock:
            self._laatste.pop(topic, None)
            self._wachtrij[topic].clear()

    def wacht_op(self, topic: str, predicate: Callable[[Any], bool] | None = None,
                 timeout: float = 15.0, vanaf_nu: bool = True) -> Any | None:
        """Wacht tot er op `topic` een bericht is dat `predicate` waarmaakt.

        vanaf_nu=True negeert eerder ontvangen berichten (kijkt enkel naar nieuwe).
        Geeft de payload terug, of None bij timeout (= mogelijke stall).
        """
        deadline = time.time() + timeout
        if vanaf_nu:
            with self._lock:
                self._wachtrij[topic].clear()
        else:
            with self._lock:
                bestaand = self._laatste.get(topic)
            if bestaand is not None and (predicate is None or predicate(bestaand)):
                return bestaand
        while time.time() < deadline:
            self._event.wait(timeout=0.2)
            self._event.clear()
            with self._lock:
                items = list(self._wachtrij[topic])
                self._wachtrij[topic].clear()
            for payload in items:
                if predicate is None or predicate(payload):
                    return payload
        return None

    # ---- callbacks ----
    def _on_connect(self, client, userdata, flags, reason_code, properties=None):
        self.verbonden.set()
        for t in self._abonnementen:
            client.subscribe(t)

    def _on_message(self, client, userdata, msg):
        try:
            payload: Any = json.loads(msg.payload.decode("utf-8"))
        except Exception:
            try:
                payload = msg.payload.decode("utf-8")
            except Exception:
                payload = msg.payload
        with self._lock:
            self._laatste[msg.topic] = payload
            self._wachtrij[msg.topic].append(payload)
        self._event.set()
