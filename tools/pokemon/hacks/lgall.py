import sys,os,subprocess
H='/home/claude/gba-ds/build/host/harness'
r=sys.argv[1]; n=os.path.basename(r)[:-4].replace('+','_')
open(f'lgall/{n}.txt','w').write(f"logo {n}\n")
e=subprocess.run([H,os.path.abspath(r),'lgall',f'lgall/{n}.txt'],capture_output=True,text=True)
print(n,[l.split('->')[0] for l in e.stderr.splitlines() if 'logo' in l][-1],flush=True)
