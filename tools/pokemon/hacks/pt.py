import sys,os,subprocess,shutil,re
H='/home/claude/gba-ds/build/host/harness'
hk={'AXVE+lightplat':'AXVE','BPEE+florasky':'BPEE','BPEE+glazed':'BPEE','BPEE+theta':'BPEE','BPRE+fuligin':'BPRE','BPRE+gaia':'BPRE','BPRE+radicalred':'BPRE','BPRE+saiph':'BPRE','BPRE+unbound':'BPRE'}
offs=[0x08000100,0x08100000,0x08400000,0x08800000,0x08C00000,0x09000000,0x09800000,0x09FFFF00,0x0A000000-0x40]
def peeks(rom,d,extra=""):
    s="".join(f"peek {o:#x} 16\n" for o in offs)+extra
    open(f'{d}/pk.txt','w').write(s)
    r=subprocess.run([H,os.path.abspath(rom),d,f'{d}/pk.txt'],capture_output=True,text=True)
    return r.stdout,r.stderr
for h,c in hk.items():
    n=h.split('+')[1]; d=f'pt/{n}'; os.makedirs(d,exist_ok=True)
    shutil.copy(f'../all/{c}.gba',f'{d}/{n}.gba'); shutil.copy(f'patches/{n}.ups',f'{d}/{n}.ups')
    a,_=peeks(f'{d}/{n}.gba',d,"logo lg\n"); b,_=peeks(h+'.gba',f'{h.replace("+","_")}')
    e=subprocess.run([H,os.path.abspath(f'{d}/{n}.gba'),d,f'{d}/pk.txt'],capture_output=True,text=True).stderr
    print(h,'ROM bytes equal' if a==b else 'DIFFER',[l for l in e.splitlines() if 'logo' in l or 'patch' in l.lower()][:2],flush=True)
