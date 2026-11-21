import sys,os,json,subprocess,shutil
sys.path.insert(0,'/home/claude/work')
from mkparty import party_bytes
from PIL import Image
H='/home/claude/gba-ds/build/host/harness'; APP='/home/claude/gba-ds/build/host/gba-ds'
R=json.load(open('results.json'))
ENG={'BPRE':(0x02024284,0x02026523),'BPGE':(0x02024284,0x02026525),'AXPE':(0x03004360,0x02026A54),'AXVE':(0x03004360,0x02026A54),'BPEE':(0x020244EC,0x02026D94)}
CNT={'BPR':0x25B,'BPG':0x25B,'AXV':0x10,'AXP':0x10,'BPE':3}
def info(b):
    if b in ENG: return ENG[b]
    return int(R[b]['party'],16),int(R[b]['flag'],16)
def harness(rom,d,script):
    open(f'{d}/f.txt','w').write(script)
    r=subprocess.run([H,rom,d,f'{d}/f.txt'],capture_output=True,text=True); return r.stdout+r.stderr
def variants(b):
    d=b.replace('+','_'); rom=os.path.abspath(b+'.gba'); party,flag=info(b); fam=b[:3]; cnt=party-CNT[fam]
    full=3 if fam in('BPR','BPG') else 7; nodex=1 if fam in('BPR','BPG') else 5
    out={}
    for name,kw in {'normal':{},'nodex':{'fv':nodex},'cursor_mid':{'cur':3},'cursor_end':{'cur':6},'one_mon':{'n':1}}.items():
        n=kw.get('n',6)
        L=["load s_p",f"pokes {party:#x} {party_bytes(n).hex()}",f"poke {cnt:#x} 1 {n}",f"poke {flag:#x} 1 {kw.get('fv',full)}","wait 20"]
        if 'cur' in kw: L+=["tap START 1","wait 25",f"tap DOWN {kw['cur']}","wait 10","tap B 1","wait 40"]
        L+=[f"map vv_{name}"]
        e=harness(rom,d,"\n".join(L)+"\n")
        out[name]=('map capture: ok' in e and 'map frames: 2' in e)
    return out
def app(b):
    d=b.replace('+','_'); rom=os.path.abspath(b+'.gba'); party,flag=info(b); cnt=party
    harness(rom,d,f"load s_p\npoke {party+100+0x56:#x} 2 78\npoke {party+400+0x50:#x} 4 0\nwait 3\nsave s_b\n")
    A=f'{d}/app'; shutil.rmtree(A,ignore_errors=True)
    for s in ('state','patch','shots'): os.makedirs(f'{A}/{s}')
    N=1800; ev=[(60,'RIGHT'),(84,'-'),(190,'START'),(194,'-'),(250,'DOWN'),(254,'-'),(380,'B'),(384,'-'),(760,'LOAD:'+os.path.abspath(f'{d}/s_b.ss'))]
    for f in range(8,N,8): ev.append((f,'SHOT:%s/shots/f%04d.bmp'%(os.path.abspath(A),f)))
    ev.append((N+1,'QUIT')); ev.sort(key=lambda x:(x[0],x[1].startswith('SHOT')))
    open(f'{A}/input.txt','w').write(''.join('%d %s\n'%e for e in ev))
    env=dict(os.environ,SDL_VIDEODRIVER='offscreen',SDL_AUDIODRIVER='dummy',SDL_RENDER_DRIVER='software',GBADS_WINDOW='1024x1536',GBADS_DEBUG='1')
    r=subprocess.run([APP,'--state-dir='+os.path.abspath(A)+'/state','--input-script='+os.path.abspath(A)+'/input.txt','--quick_bar_hint=0','--overlays=off','--patch_dir='+os.path.abspath(A)+'/patch','--load-state='+os.path.abspath(f'{d}/s_p.ss'),rom],env=env,capture_output=True,text=True,timeout=900)
    open(f'{A}/log.txt','w').write(r.stdout+r.stderr)
    # analyse the bottom screen
    kinds=[];frames=[]
    import glob
    for f in sorted(glob.glob(f'{A}/shots/f*.bmp')):
        n=int(os.path.basename(f)[1:5]); im=Image.open(f).convert('RGB').crop((0,768,1024,1536)).resize((128,96))
        px=list(im.getdata()); tot=len(px)
        nonblack=sum(1 for p in px if sum(p)>60)/tot
        water=sum(1 for p in px if p[2]>235 and p[0]<180)/tot
        kinds.append((n,nonblack,water)); frames.append((n,px))
    late=[k for k in kinds if k[0]>=600]
    shown=sum(1 for k in late if k[1]>0.3)/max(1,len(late))
    maps=sum(1 for k in late if k[2]>0.12); party=sum(1 for k in late if k[1]>0.3 and k[2]<0.05)
    pre=[px for n,px in frames if 560<=n<760 and next(k for k in kinds if k[0]==n)[2]<0.05 and next(k for k in kinds if k[0]==n)[1]>0.3]
    post=[px for n,px in frames if n>=1000 and next(k for k in kinds if k[0]==n)[2]<0.05 and next(k for k in kinds if k[0]==n)[1]>0.3]
    diff=sum(1 for a,b2 in zip(pre[-1],post[0]) if sum(abs(x-y) for x,y in zip(a,b2))>60) if pre and post else -1
    for t in (104,448,904,1504):
        f=min(glob.glob(f'{A}/shots/f*.bmp'),key=lambda p:abs(int(os.path.basename(p)[1:5])-t))
        Image.open(f).convert('RGB').resize((512,768)).save(f'{d}/keep_{t}.png')
    shutil.rmtree(f'{A}/shots',ignore_errors=True)
    return dict(shown=round(shown,2),map_frames=maps,party_frames=party,heal_diff=diff)
if __name__=='__main__':
    res={}
    if os.path.exists('full_results.json'): res=json.load(open('full_results.json'))
    for b in sys.argv[1:]:
        if b in res: continue
        v=variants(b); a=app(b); res[b]=dict(variants=v,app=a)
        cur=json.load(open('full_results.json')) if os.path.exists('full_results.json') else {}
        cur[b]=res[b]; json.dump(cur,open('full_results.json','w'))
        print(b,v,a,flush=True)
