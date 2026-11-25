import sys,os,json,re,subprocess
sys.path.insert(0,'/home/claude/work')
from mkparty import party_bytes
H='/home/claude/gba-ds/build/host/harness'
BASE={'BPG':dict(party=0x02024284,count=0x02024029,sb1p=0x03005008,sb2p=0x0300500C),'AXP':dict(party=0x03004360,count=0x03004350,sb1=0x02025734),'BPR':dict(party=0x02024284,count=0x02024029,sb1p=0x03005008,sb2p=0x0300500C),
      'BPE':dict(party=0x020244EC,count=0x020244E9,sb1p=0x03005D8C),
      'AXV':dict(party=0x03004360,count=0x03004350,sb1=0x02025734)}
def harness(rom,d,script):
    open(f'{d}/r.txt','w').write(script)
    r=subprocess.run([H,os.path.abspath(rom),d,f'{d}/r.txt'],capture_output=True,text=True); return r.stdout+r.stderr
def peek(rom,d,addr,n=4):
    e=harness(rom,d,f"load s_new\npeek {addr:#x} {n}\n")
    m=re.search(r'%08X: ([0-9A-F ]+)'%addr,e,re.I)
    return bytes(int(x,16) for x in m.group(1).split()) if m else None
def main(rom,party_over=None,out='pg',extra_wait=20):
    base=rom[:-4]; d=base.replace('+','_'); fam=base[:3]; b=dict(BASE[fam]); fam={'BPG':'BPR','AXP':'AXV'}.get(fam,fam)
    party=b['party']; count=b['count']
    L=["load s_new"]
    if fam=='AXV': flag=b['sb1']+0x1320; extra=[]
    else:
        ptr=int.from_bytes(peek(rom,d,b['sb1p']),'little')
        if fam=='BPE': flag=ptr+0x137C; extra=[]
        else:
            ptr2=int.from_bytes(peek(rom,d,b['sb2p']),'little'); k=peek(rom,d,ptr2+0xF20); key=int.from_bytes(k[:2],'little')
            kp=int.from_bytes(peek(rom,d,0x0203988C+8),'little')
            slot=kp
            for i in range(8):
                v=peek(rom,d,kp+4*i)
                if int.from_bytes(v[:2],'little')==0: slot=kp+4*i;break
            flag=ptr+0xEE0+0x105; extra=[f"poke {slot:#x} 4 {((1^key)<<16)|0x169:#x}"]
    fv=3 if fam=='BPR' else 7
    L+=[f"pokes {party:#x} {party_bytes(6).hex()}",f"poke {count:#x} 1 6",f"poke {flag:#x} 1 {fv}"]+extra+[f"wait {extra_wait}","save s_p","wait 2","shot p0","tap START 1","wait 40","shot p1",f"load s_p",f"map {out}"]
    e=harness(rom,d,"\n".join(L)+"\n")
    cap=[l for l in e.splitlines() if 'map capture' in l or 'map frames' in l or 'rror' in l]
    print(base,cap,flush=True)
if __name__=='__main__':
    for r in sys.argv[1:]: main(r)
