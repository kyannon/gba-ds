import sys,os,subprocess
from PIL import Image
H='/home/claude/gba-ds/build/host/harness'
def run(rom,d,script):
    open(f'{d}/s.txt','w').write(script)
    return subprocess.run([H,os.path.abspath(rom),d,f'{d}/s.txt'],capture_output=True,text=True).stderr
def panel_ok(png):
    im=Image.open(png).convert('RGB').crop((0,0,240,160)); w=0
    for y in range(4,100):
        for x in range(172,236):
            r,g,b=im.getpixel((x,y)); w+= (r>240 and g>240 and b>240)
    return w>=900
def probe(rom,d,st):
    run(rom,d,f"load {st}\ntap START 1\nwait 40\nshot probe\n")
    return panel_ok(f'{d}/probe.png')
def main(rom):
    code=rom[:4]; d=rom.split('.')[0].replace('+','_'); os.makedirs(d,exist_ok=True)
    frlg=code[:3] in ('BPR','BPG')
    L=["wait 300"]
    for i in range(14): L+=["tap START 1","wait 50"]
    L+=["wait 200"]
    gap=60 if frlg else 45
    for i in range(160 if frlg else 200): L+=["tap A 1",f"wait {gap}"]
    L+=["tap B 4","wait 20"]
    if not frlg: L+=["hold RIGHT 40","hold DOWN 30","hold RIGHT 40","hold DOWN 30","wait 100"]
    L+=["save s_new"]
    run(rom,d,"\n".join(L)+"\n")
    stages=1
    while not probe(rom,d,'s_new') and stages<8:
        M=["load s_new"]
        for i in range(70): M+=["tap A 1","wait 45"]
        M+=["tap B 3","wait 20"]
        if not frlg: M+=["hold RIGHT 40","hold DOWN 30","wait 60"]
        M+=["save s_new"]
        run(rom,d,"\n".join(M)+"\n"); stages+=1
    ok=probe(rom,d,'s_new')
    run(rom,d,"load s_new\nshot end\n")
    print(rom,'control' if ok else 'NO CONTROL','stages',stages,flush=True)
if __name__=='__main__':
    for r in sys.argv[1:]: main(r)
