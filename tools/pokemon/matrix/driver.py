import os,time,sys,json
sys.path.insert(0,'.')
import runone
todo=[l.strip() for l in open('xl.txt')]
done=set(); res={}
if os.path.exists('results.json'): res=json.load(open('results.json')); done=set(res)
while len(done)<len(todo):
    prog=False
    for r in todo:
        b=r[:-4]
        if b in done or not os.path.exists(f'xr/{b}.json'): continue
        res[b]=runone.main(r); done.add(b); prog=True
        json.dump(res,open('results.json','w'))
    if not prog: time.sleep(20)
print('ALL DONE',flush=True)
