#!/usr/bin/python3
"""Retry missing thyme sensors before the desktop starts; never unbind devices."""
from pathlib import Path
import sys
import time

model = Path('/sys/firmware/devicetree/base/model')
if not model.exists() or model.read_bytes().rstrip(b'\0') != b'Xiaomi Mi 10S (thyme)':
    sys.exit(0)

sensors = {
    '21-003d': 's5khmx',
    '22-0010': 's5k3t2',
    '23-0036': 'ov13b10',
}
pending = dict(sensors)
for attempt in range(1, 6):
    for address, driver in list(pending.items()):
        device = Path('/sys/bus/i2c/devices') / address
        binding = device / 'driver'
        expected = Path('/sys/bus/i2c/drivers') / driver
        if binding.is_symlink():
            if binding.resolve() != expected:
                print(f'{address}: unexpected driver; leaving it alone', flush=True)
                sys.exit(1)
            del pending[address]
            continue
        if not device.exists() or not (expected / 'bind').exists():
            continue
        print(f'{address}: retrying {driver} probe ({attempt}/5)', flush=True)
        try:
            (expected / 'bind').write_text(address + '\n')
        except OSError as error:
            print(f'{address}: {error}', flush=True)
        if binding.is_symlink() and binding.resolve() == expected:
            print(f'{address}: recovered', flush=True)
            del pending[address]
    if not pending:
        print('All three thyme camera sensors are bound', flush=True)
        sys.exit(0)
    if attempt < 5:
        time.sleep(1)

print('Camera sensors still missing: ' + ', '.join(pending), flush=True)
sys.exit(1)
