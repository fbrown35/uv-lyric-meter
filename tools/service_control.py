"""Public service diagnostics and recognition trigger; no credentials are transmitted."""
import argparse,time
from serial_link import open_device
parser=argparse.ArgumentParser()
parser.add_argument('command',choices=['DIAG','RECOGNIZE'])
args=parser.parse_args()
port=open_device();port.write(('CONTROL '+args.command+'\n').encode());port.flush()
end=time.time()+12;pending=b''
while time.time()<end:
    pending+=port.read(65536)
    if b'CONTROL_OK' in pending:
        for line in pending.splitlines():
            for marker in (b'SERVICE_DIAG',b'Manual recognition requested',b'CONTROL_OK'):
                position=line.find(marker)
                if position>=0:
                    start=line.rfind(b'I (',0,position)
                    if start<0:start=position
                    print(line[start:].decode(errors='replace'));break
        break
    if b'CONTROL_FAILED' in pending:raise SystemExit('Command rejected')
    if len(pending)>131072:pending=pending[-65536:]
else:raise SystemExit('No acknowledgement')
port.close()
