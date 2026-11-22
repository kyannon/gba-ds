#!/usr/bin/env python3
# st.py ROM IN OUT "script"  : load IN, run script lines (; separated), save OUT, shot OUT
import sys,os,subprocess
H='/home/claude/gba-ds/build/host/harness'
rom,i,o,sc=sys.argv[1:5]; d=rom[:-4].replace('+','_')
s=f"load {i}\nwait 2\n"+sc.replace(';','\n')+f"\nsave {o}\nwait 2\nshot {o}\n"
open(f'{d}/st.txt','w').write(s)
subprocess.run([H,os.path.abspath(rom),d,f'{d}/st.txt'],capture_output=True)
