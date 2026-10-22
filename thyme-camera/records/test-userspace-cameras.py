#!/usr/bin/env python3
"""Run as the phone's desktop user, with no other camera client streaming."""
import json
from pathlib import Path
import subprocess
import time

out = Path('/tmp/thyme-camera-userspace-tests')
out.mkdir(exist_ok=True)
cameras = {
    'ov13b10': ('ac50000', '1', '36'),
    's5k3t2': ('ac50000', '0', '10'),
    's5khmx': ('ac4f000', '1', '3d'),
}

def service_state():
    return subprocess.check_output([
        'systemctl', '--user', 'show', 'pipewire.service',
        '-p', 'MainPID', '-p', 'NRestarts', '-p', 'ActiveState',
    ], text=True).strip()

for attempt in range(20):
    try:
        nodes = json.loads(subprocess.check_output(['pw-dump'], timeout=2))
        if sum(n.get('info', {}).get('props', {}).get('node.name', '').startswith(
                'libcamera_input.') for n in nodes) == 3:
            break
    except (subprocess.TimeoutExpired, subprocess.CalledProcessError):
        pass
    time.sleep(0.5)
else:
    raise SystemExit('Three camera nodes did not appear')

before = service_state()
print('BEFORE\n' + before, flush=True)
results = []
sequence = [
    ('ov13b10', 640, 480), ('s5k3t2', 640, 480),
    ('s5khmx', 640, 480), ('ov13b10', 1280, 720),
    ('s5k3t2', 1280, 720), ('ov13b10', 640, 480),
]
for index, (name, width, height) in enumerate(sequence):
    cci, bus, address = cameras[name]
    node = f'libcamera_input._base_soc_0_cci_{cci}_i2c-bus_{bus}_camera_{address}'
    command = [
        'timeout', '-k', '3', '30', 'gst-launch-1.0', '-e',
        'pipewiresrc', f'target-object={node}', 'num-buffers=60',
        '!', f'video/x-raw,width={width},height={height}',
        '!', 'fakesink', 'sync=false',
    ]
    start = time.monotonic()
    run = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    log = f'{index + 1}-{name}-{width}x{height}.log'
    (out / log).write_bytes(run.stdout)
    result = dict(sensor=name, width=width, height=height,
                  buffers=60, exit=run.returncode,
                  eos=b'Got EOS' in run.stdout,
                  seconds=round(time.monotonic() - start, 2), log=log)
    results.append(result)
    print(json.dumps(result), flush=True)
    if run.returncode or not result['eos']:
        print(run.stdout.decode(errors='replace'), flush=True)
        break
    time.sleep(0.5)

after = service_state()
print('AFTER\n' + after, flush=True)
(out / 'results.json').write_text(json.dumps(dict(before=before, after=after,
                                                results=results), indent=2) + '\n')
raise SystemExit(0 if len(results) == len(sequence) and before == after
                 and all(r['exit'] == 0 and r['eos'] for r in results) else 1)
