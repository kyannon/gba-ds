import sys,os,subprocess
sys.path.insert(0,'/home/claude/work')
from mkparty import party_bytes
H='/home/claude/gba-ds/build/host/harness'
rom=sys.argv[1]; d=rom[:-4].replace('+','_')
for c in sys.argv[2:]:
    a=int(c,16)
    s=f"load s_new\nwait 2\npokes {a:#x} {party_bytes(6).hex()}\npoke {a-3:#x} 1 6\nwait 5\nhold START 3\nwait 40\nshot cand_{c}\n"
    open(f'{d}/c.txt','w').write(s)
    subprocess.run([H,os.path.abspath(rom),d,f'{d}/c.txt'],capture_output=True)
