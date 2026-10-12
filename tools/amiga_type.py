#!/usr/bin/env python3
"""Type through the user's installed Amiberry IPC skill (Amiga raw keycodes)."""
import subprocess, sys, os
from pathlib import Path
client=Path(os.environ['AMIBERRY_IPC_CLIENT']) if 'AMIBERRY_IPC_CLIENT' in os.environ else next(iter(sorted((Path.home()/'.codex/plugins/cache/amiberry-agent-tools/amiberry-ipc-control').glob('*/skills/amiberry-ipc-control/scripts/amiberry_ipc.py'))), None)
if client is None:
    raise SystemExit('Install the Amiberry IPC skill or set AMIBERRY_IPC_CLIENT.')
keys={c:i+0x10 for i,c in enumerate('qwertyuiop')}
keys.update({c:i+0x20 for i,c in enumerate('asdfghjkl;')})
keys.update({c:i+0x31 for i,c in enumerate('zxcvbnm,./')})
keys.update({c:i+1 for i,c in enumerate('1234567890-=')})
keys.update({' ':0x40,'\n':0x44})
commands=[]
for c in sys.argv[1]:
    shift=c.isupper() or c in ':><'
    if c==':': c=';'
    elif c=='>': c='.'
    elif c=='<': c=','
    code=keys[c.lower()]
    if shift: commands += ['SEND_KEY 96 1']
    commands += [f'SEND_KEY {code} 1','SLEEP 0.04',f'SEND_KEY {code} 0','SLEEP 0.04']
    if shift: commands += ['SEND_KEY 96 0']
subprocess.run([sys.executable,str(client),'batch',*commands],check=True,stdout=subprocess.DEVNULL)
