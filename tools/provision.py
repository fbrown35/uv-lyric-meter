"""Send local secrets to device NVS without echoing credentials.
Run with the ESP-IDF Python environment: python tools/provision.py
"""
import json, pathlib, time
from serial_link import open_device
root = pathlib.Path(__file__).resolve().parent.parent
try:
    values = json.loads((root / '.secrets.json').read_text())
    for key, limit in [('wifi_ssid',32),('wifi_password',64),('audd_token',255)]:
        if not isinstance(values.get(key),str) or len(values[key].encode()) > limit:
            raise ValueError('invalid field')
    if not values['wifi_ssid'] or not values['audd_token']:
        raise ValueError('required field empty')
except Exception:
    raise SystemExit('Private configuration is missing or invalid. Values withheld.')
port = open_device()
port.write(('PROVISION ' + json.dumps(values,ensure_ascii=True) + '\n').encode()); port.flush()
end=time.time()+12
pending=b''
while time.time()<end:
    pending+=port.read(65536)
    if b'PROVISION_OK' in pending:
        port.close(); print('Credentials saved to device NVS. Values withheld.'); break
    if b'PROVISION_FAILED' in pending:
        port.close(); raise SystemExit('Device rejected provisioning. Values withheld.')
    if len(pending)>131072: pending=pending[-65536:]
else:
    port.close(); raise SystemExit('No provisioning acknowledgement. Values withheld.')
