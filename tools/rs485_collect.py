#!/usr/bin/env python3
"""Undeployed stdin-to-NDJSON adapter. Never connects to MQTT or publishes."""
import argparse
import datetime as dt
import json
import os
import sys

TOPICS = {
    "balboa/waveshare/diagnostics/rs485",
    "balboa/waveshare/diagnostics/rs485/events/v1",
}
MAX_LINE = 32768


def parse_record(line, received_utc=None):
    if len(line) > MAX_LINE:
        raise ValueError("oversized diagnostic record")
    topic, payload = line.decode("utf-8").strip().split(" ", 1)
    if topic not in TOPICS:
        raise ValueError("unexpected topic")
    data = json.loads(payload)
    if not isinstance(data, dict) or data.get("schema_version") != 1:
        raise ValueError("unsupported schema")
    if topic.endswith("events/v1"):
        events = data.get("events")
        if not isinstance(events, list) or len(events) > 16:
            raise ValueError("invalid event batch")
    received = received_utc or dt.datetime.now(dt.timezone.utc)
    if received.tzinfo is None or received.utcoffset() != dt.timedelta(0):
        raise ValueError("collector timestamp must be UTC")
    return {"received_utc": received.isoformat(), "topic": topic, "payload": data}


def records(stream):
    while True:
        line = stream.readline(MAX_LINE + 1)
        if not line:
            return
        if len(line) > MAX_LINE:
            while line and not line.endswith(b"\n"):
                line = stream.readline(MAX_LINE + 1)
            continue
        try:
            yield parse_record(line)
        except (ValueError, UnicodeError, RecursionError):
            # Do not print potentially sensitive/malformed input.
            print("Rejected diagnostic input", file=sys.stderr)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", help="append-only NDJSON file; arrange external rotation")
    args = parser.parse_args()
    fd = os.open(args.output, os.O_WRONLY | os.O_CREAT | os.O_APPEND, 0o600)
    with os.fdopen(fd, "a", encoding="utf-8") as output:
        for record in records(sys.stdin.buffer):
            output.write(json.dumps(record, separators=(",", ":")) + "\n")
            output.flush()


if __name__ == "__main__":
    main()
