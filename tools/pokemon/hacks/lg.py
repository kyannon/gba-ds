import sys,os,subprocess
H='/home/claude/gba-ds/build/host/harness'
for r in sys.argv[2:]:
    d=r[:-4].replace('+','_'); os.makedirs(d,exist_ok=True)
    open(f'{d}/lg.txt','w').write(f"logo {sys.argv[1]}\n")
    e=subprocess.run([H,os.path.abspath(r),d,f'{d}/lg.txt'],capture_output=True,text=True)
    print(r,[l for l in e.stderr.splitlines() if 'logo' in l])
