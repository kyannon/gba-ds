import subprocess,sys,json,os,re
X='/home/claude/gba-ds/tools/romxref.py'
fam={'BPR':('BPRE.gba',['0x02024284','0x03005008']),'BPG':('BPRE.gba',['0x02024284','0x03005008']),
     'AXV':('AXVE.gba',['0x03004360','0x02025734']),'AXP':('AXVE.gba',['0x03004360','0x02025734']),
     'BPE':('BPEE.gba',['0x020244EC','0x03005D8C'])}
for f in sys.argv[1:]:
    out=f'xr/{f[:-4]}.json'
    if os.path.exists(out): continue
    src,addrs=fam[f[:3]]
    r=subprocess.run(['python3',X,src,f]+addrs,capture_output=True,text=True).stdout
    res={}
    for l in r.splitlines():
        m=re.match(r'(0x[0-9a-f]+) -> (\{.*\})',l)
        if m: res[m.group(1)]=eval(m.group(2))
    json.dump(res,open(out,'w')); print(f,res,flush=True)
