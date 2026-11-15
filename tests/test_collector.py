import datetime as dt
import importlib.util
import io
from pathlib import Path
import unittest
import json
import subprocess
import sys
import tempfile

spec = importlib.util.spec_from_file_location("collector", Path(__file__).parents[1] / "tools/rs485_collect.py")
c = importlib.util.module_from_spec(spec)
spec.loader.exec_module(c)


class CollectorTests(unittest.TestCase):
    def test_receipt_time_does_not_replace_unsynchronized_device_clock(self):
        when = dt.datetime(2026, 11, 14, tzinfo=dt.timezone.utc)
        r = c.parse_record(b'balboa/waveshare/diagnostics/rs485/events/v1 {"schema_version":1,"utc_s":null,"events":[]}', when)
        self.assertEqual(r["received_utc"], when.isoformat())
        self.assertIsNone(r["payload"]["utc_s"])

    def test_rejects_unrelated_topics_and_bad_batches(self):
        for line in [b'balboa/waveshare/fan/command {"schema_version":1}',
                     b'balboa/waveshare/diagnostics/rs485 []',
                     b'balboa/waveshare/diagnostics/rs485 {"schema_version":2}',
                     b'balboa/waveshare/diagnostics/rs485/events/v1 {"schema_version":1}',
                     b'x' * (c.MAX_LINE + 1)]:
            with self.assertRaises(ValueError):
                c.parse_record(line)

    def test_bounded_stream_recovers_after_oversized_record(self):
        good = b'balboa/waveshare/diagnostics/rs485 {"schema_version":1}\n'
        r = list(c.records(io.BytesIO(b'x' * (c.MAX_LINE * 3) + b'\n' + good)))
        self.assertEqual(len(r), 1)
        self.assertEqual(r[0]["topic"], "balboa/waveshare/diagnostics/rs485")

    def test_rejects_naive_collector_time(self):
        with self.assertRaises(ValueError):
            c.parse_record(b'balboa/waveshare/diagnostics/rs485 {"schema_version":1}', dt.datetime(2026, 1, 1))


    def test_cli_replay_appends_private_ndjson_without_network(self):
        with tempfile.TemporaryDirectory() as temp:
            output = Path(temp) / "diagnostics.ndjson"
            line = b'balboa/waveshare/diagnostics/rs485 {"schema_version":1}\n'
            for _ in range(2):
                subprocess.run([sys.executable, str(Path(c.__file__)), str(output)],
                               input=line, check=True, capture_output=True, timeout=5)
            rows = [json.loads(row) for row in output.read_text().splitlines()]
            self.assertEqual(len(rows), 2)
            self.assertEqual(rows[0]["topic"], "balboa/waveshare/diagnostics/rs485")
            self.assertEqual(output.stat().st_mode & 0o777, 0o600)
