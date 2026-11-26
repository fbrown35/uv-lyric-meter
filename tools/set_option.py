"""Change a public runtime setting over USB, without reflashing or sending credentials."""
import argparse,json,time
from serial_link import open_device
parser=argparse.ArgumentParser()
parser.add_argument('--lyric-delay-ms',type=int)
parser.add_argument('--vu-ref',type=int)
parser.add_argument('--screen',type=int,choices=range(4))
parser.add_argument('--style',type=int,choices=range(2))
parser.add_argument('--demo',type=int,choices=range(2))
parser.add_argument('--get',action='store_true')
args=parser.parse_args()
settings={k:v for k,v in vars(args).items() if k!='get' and v is not None}
if 'demo' in settings:settings['demo']=bool(settings['demo'])
if not settings and not args.get:parser.error('provide a setting or --get')
port=open_device()
def command(text,marker):
    port.write((text+'\n').encode());port.flush();end=time.time()+12;pending=b''
    while time.time()<end:
        pending+=port.read(65536)
        for line in pending.splitlines():
            if marker in line:
                position=line.find(marker)
                start=line.rfind(b'I (',0,position)
                if start<0:start=position
                print(line[start:].decode(errors='replace'));return
            if b'SETTINGS_FAILED' in line:raise SystemExit('Setting rejected')
        if len(pending)>131072:pending=pending[-65536:]
    raise SystemExit('No acknowledgement')
if settings:command('SETTINGS '+json.dumps(settings),b'SETTINGS_OK')
command('GET_SETTINGS',b'SETTINGS_STATE')
port.close()
