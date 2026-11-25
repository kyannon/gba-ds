import sys,os,subprocess
H='/home/claude/gba-ds/build/host/harness'
for rom in sys.argv[1:]:
    d=rom[:-4].replace('+','_'); s=""
    for i in range(12):
        s+="wait 100\n"+f"shot t{i:02d}\n"+("hold START 3\n" if i<8 else "")
    open(f'{d}/tt.txt','w').write(s)
    subprocess.run([H,os.path.abspath(rom),d,f'{d}/tt.txt'],capture_output=True)
