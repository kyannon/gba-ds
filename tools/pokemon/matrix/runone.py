import sys,os,json,re,subprocess
sys.path.insert(0,'/home/claude/work')
from mkparty import party_bytes
H='/home/claude/gba-ds/build/host/harness'
OFF={'BPR':dict(count=0x25B),'BPG':dict(count=0x25B),'AXV':dict(count=0x10),'AXP':dict(count=0x10),'BPE':dict(count=3)}
def harness(rom,d,script):
    open(f'{d}/r.txt','w').write(script)
    r=subprocess.run([H,os.path.abspath(rom),d,f'{d}/r.txt'],capture_output=True,text=True); return r.stdout+r.stderr
def peek(rom,d,addr,n=4):
    e=harness(rom,d,f"load s_new\npeek {addr:#x} {n}\n")
    m=re.search(r'%08X: ([0-9A-F ]+)'%addr,e)
    return bytes(int(x,16) for x in m.group(1).split()) if m else None
def best(c,top):
    c={int(k,16):v for k,v in c.items() if (int(k,16)>>24)==top}
    return max(c,key=c.get) if c else None
def main(rom):
    base=rom[:-4]; d=base.replace('+','_'); fam=base[:3]
    x=json.load(open(f'xr/{base}.json'))
    ks=list(x)
    if fam in ('BPR','BPG'): top=2; ptop=2
    elif fam in ('AXV','AXP'): top=3
    else: top=2
    party=best(x[ks[0]],3 if fam in ('AXV','AXP') else 2)
    sb=best(x[ks[1]],3 if fam in ('BPR','BPG','BPE') else 2)
    info={'party':party and hex(party),'sb':sb and hex(sb)}
    if not party or not sb: print(base,'NO ADDRESS',info,flush=True); return info
    count=party-OFF[fam]['count']
    L=["load s_new"]
    if fam in ('AXV','AXP'):
        flag=sb+0x1320; extra=[]
    else:
        p1=peek(rom,d,sb); 
        if not p1: print(base,'peek failed'); return info
        ptr=int.from_bytes(p1,'little'); info['sb1']=hex(ptr)
        if fam=='BPE': flag=ptr+0x137C; extra=[]
        else:
            p2=peek(rom,d,sb+4); ptr2=int.from_bytes(p2,'little'); k=peek(rom,d,ptr2+0xF20); key=int.from_bytes(k[:2],'little')
            flag=ptr+0xEE0+0x105; extra=[f"poke {ptr+0x3B8:#x} 4 {((1^key)<<16)|0x169:#x}"]
    fv=3 if fam in ('BPR','BPG') else 7
    L+=[f"pokes {party:#x} {party_bytes(6).hex()}",f"poke {count:#x} 1 6",f"poke {flag:#x} 1 {fv}"]+extra+["wait 20","save s_p","map pg"]
    e=harness(rom,d,"\n".join(L)+"\n")
    cap=[l for l in e.splitlines() if 'map capture' in l or 'map frames' in l]
    info['result']=cap; info['flag']=hex(flag)
    print(base,info,flush=True); return info
if __name__=='__main__':
    for r in sys.argv[1:]: main(r)
