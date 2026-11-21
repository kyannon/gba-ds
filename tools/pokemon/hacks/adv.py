import sys,os,subprocess
sys.path.insert(0,'.')
from intro2 import run,probe
# adv.py ROM PRE_SCRIPT_FILE : pre script runs from s_new and saves s_new2; then batches of A until control
rom=sys.argv[1]; d=rom[:-4].replace('+','_'); pre=open(sys.argv[2]).read() if len(sys.argv)>2 else ""
run(rom,d,"load s_new\nwait 3\n"+pre+"save s_a\n")
n=0
while n<60 and not probe(rom,d,'s_a'):
    M=["load s_a"]
    for i in range(4): M+=["tap A 1","wait 45"]
    M+=["save s_a"]; run(rom,d,"\n".join(M)+"\n"); n+=1
ok=probe(rom,d,'s_a')
print(rom,'control' if ok else 'NO CONTROL',n)
if ok: run(rom,d,"load s_a\nsave s_new\n")
run(rom,d,"load s_a\nwait 3\nshot end\n")
