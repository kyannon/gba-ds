import sys,os
sys.path.insert(0,'.')
from intro2 import run,probe
def main(rom):
    d=rom.split('.')[0].replace('+','_'); r=0; ok=probe(rom,d,'s_new')
    while not ok and r<40:
        M=["load s_new"]
        for i in range(5): M+=["tap A 1","wait 45"]
        M+=["wait 60","save s_new"]
        run(rom,d,"\n".join(M)+"\n"); r+=1; ok=probe(rom,d,'s_new')
    run(rom,d,"load s_new\nshot end\n")
    print(rom,'control' if ok else 'NO CONTROL','rounds',r,flush=True)
for r in sys.argv[1:]: main(r)
