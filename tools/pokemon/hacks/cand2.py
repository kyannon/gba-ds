import sys,os,subprocess,re
sys.path.insert(0,'/home/claude/work')
from mkparty import party_bytes
H='/home/claude/gba-ds/build/host/harness'
rom=sys.argv[1]; d=rom[:-4].replace('+','_'); sbp=int(sys.argv[2],16); off=int(sys.argv[3],16)
def h(s):
    open(f'{d}/c.txt','w').write(s); r=subprocess.run([H,os.path.abspath(rom),d,f'{d}/c.txt'],capture_output=True,text=True); return r.stdout+r.stderr
e=h(f"load s_new\npeek {sbp:#x} 4\n"); m=re.search(r'%08X: ([0-9A-F ]+)'%sbp,e); ptr=int.from_bytes(bytes(int(x,16) for x in m.group(1).split()),'little')
print('sb1',hex(ptr))
for c in sys.argv[4:]:
    a=int(c,16)
    h(f"load s_new\nwait 2\npokes {a:#x} {party_bytes(6).hex()}\npoke {a-3:#x} 1 6\npoke {ptr+off:#x} 1 7\nwait 5\nhold START 3\nwait 40\nshot cd_{c}\n")
