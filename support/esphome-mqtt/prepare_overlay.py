#!/usr/bin/env python3
"""Prepare a checksum-guarded local MQTT override; never modifies the installation."""
import argparse
import difflib
import hashlib
import json
from pathlib import Path
import shutil

EXPECTED = {
    'mqtt_component.h': '7944e823a3db9287694cff589bae81f5614f27bf8e72eaae099513d6970c8aee',
    'mqtt_component.cpp': '3bb2f92764599448cfd621c07a00662494524923ee099affe50388f0a56053d2',
}


def replace_once(text, old, new):
    if text.count(old) != 1:
        raise ValueError('Installed MQTT source does not match the reviewed patch context')
    return text.replace(old, new, 1)


def prepare(source, output):
    source, output = Path(source).resolve(), Path(output).resolve()
    for name, expected in EXPECTED.items():
        if hashlib.sha256((source / name).read_bytes()).hexdigest() != expected:
            raise ValueError(f'Unsupported MQTT source hash: {name}; reviewed ESPHome 2026.10.2 required')
    if output.exists():
        raise ValueError('Choose a new output directory; existing artifacts are preserved')
    target = output / 'components' / 'mqtt'
    target.parent.mkdir(parents=True)
    shutil.copytree(source, target, ignore=shutil.ignore_patterns('__pycache__', '*.pyc'))
    header = (source / 'mqtt_component.h').read_text()
    header = replace_once(header, '#include "mqtt_client.h"',
                          '#include "mqtt_client.h"\n#include "mqtt_retry_gate.h"\n#include "esphome/core/hal.h"')
    header = replace_once(header, 'bool is_resend_pending() const { return this->resend_state_; }',
                          'bool is_resend_pending() const { return this->resend_state_ && this->retry_gate_.ready(millis()); }')
    header = replace_once(header, '  // Packed bitfields - QoS values are 0-2, bools are flags',
                          '  MqttRetryGate retry_gate_;\n\n  // Packed bitfields - QoS values are 0-2, bools are flags')
    cpp = (source / 'mqtt_component.cpp').read_text()
    start = cpp.index('void MQTTComponent::process_resend() {')
    end = cpp.index('void MQTTComponent::schedule_resend_state()', start)
    method = cpp[start:end]
    method = replace_once(method, 'if (!this->resend_state_)', 'if (!this->is_resend_pending())')
    method = replace_once(method, '  if (!this->send_initial_state()) {\n    this->schedule_resend_state();\n  }\n}',
                          '  if (!this->send_initial_state()) {\n    this->schedule_resend_state();\n  }\n'
                          '  if (this->resend_state_) this->retry_gate_.failed(millis());\n'
                          '  else this->retry_gate_.succeeded();\n}')
    cpp = cpp[:start] + method + cpp[end:]
    (target / 'mqtt_component.h').write_text(header)
    (target / 'mqtt_component.cpp').write_text(cpp)
    helper = Path(__file__).with_name('mqtt_retry_gate.h')
    shutil.copy2(helper, target / helper.name)
    patch = ''
    for name in EXPECTED:
        patch += ''.join(difflib.unified_diff((source / name).read_text().splitlines(True),
                                             (target / name).read_text().splitlines(True),
                                             fromfile='a/' + name, tofile='b/' + name))
    (output / 'mqtt-paced-retry.patch').write_text(patch)
    manifest = {'reviewed_esphome': '2026.10.2', 'source_hashes': EXPECTED,
                'patched_hashes': {n: hashlib.sha256((target / n).read_bytes()).hexdigest()
                                   for n in [*EXPECTED, helper.name]}}
    (output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    return target


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    print(prepare(args.source, args.output))
