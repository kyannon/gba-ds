import sys,os,subprocess
H='/home/claude/gba-ds/build/host/harness'
K={'AXVE+lightplat':4,'BPEE+florasky':3,'BPEE+glazed':3,'BPEE+mariomon':3,'BPEE+rogueex':0,'BPEE+theta':3,'BPRE+fuligin':3,'BPRE+gaia':3,'BPRE+radicalred':3,'BPRE+saiph':1,'BPRE+unbound':3}
for r,k in K.items():
    d=r.replace('+','_'); s=""
    for i in range(k): s+="wait 100\nhold START 3\n"
    s+="wait 100\n" if r!='BPEE+rogueex' else "wait 700\n"
    s+="shot ti\nlayershot ly\n"
    open(f'{d}/ly.txt','w').write(s)
    subprocess.run([H,os.path.abspath(r+'.gba'),d,f'{d}/ly.txt'],capture_output=True)
    print(r,sorted(f for f in os.listdir(d) if f.startswith('ly')))
