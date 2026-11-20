import sys,os,subprocess,re
sys.path.insert(0,'/home/claude/work')
from mkparty import party_bytes
H='/home/claude/gba-ds/build/host/harness'
rom=sys.argv[1]; d=rom[:-4].replace('+','_'); sbp=int(sys.argv[2],16); party=int(sys.argv[3],16)
def h(s):
    open(f'{d}/c.txt','w').write(s); r=subprocess.run([H,os.path.abspath(rom),d,f'{d}/c.txt'],capture_output=True,text=True); return r.stdout+r.stderr
e=h(f"load s_new\npeek {sbp:#x} 4\n"); m=re.search(r'%08X: ([0-9A-F ]+)'%sbp,e); ptr=int.from_bytes(bytes(int(x,16) for x in m.group(1).split()),'little')
for lo,hi in [(0x4000,0x6000),(0x6000,0x8000)]:
    h(f"load s_new\nwait 2\npokes {party:#x} {party_bytes(6).hex()}\npoke {party-3:#x} 1 6\npokes {ptr+lo:#x} {'ff'*(hi-lo)}\nwait 5\nhold START 3\nwait 40\nshot q_{lo:x}\n")
