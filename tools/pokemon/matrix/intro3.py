import sys,os
sys.path.insert(0,'.')
from intro2 import run,probe
def main(rom):
    d=rom.split('.')[0].replace('+','_'); st=0; ok=False
    while not ok and st<5:
        M=["load s_new","tap B 4","wait 30","hold RIGHT 40","hold DOWN 30","hold RIGHT 40","hold DOWN 30","wait 100",f"shot w{st}"]
        for i in range(45): M+=["tap A 1","wait 40"]
        M+=["tap B 3","wait 60","hold DOWN 10","wait 30","save s_new"]
        run(rom,d,"\n".join(M)+"\n"); st+=1; ok=probe(rom,d,'s_new')
    run(rom,d,"load s_new\nshot end\n")
    print(rom,'control' if ok else 'NO CONTROL','stages',st,flush=True)
for r in sys.argv[1:]: main(r)
