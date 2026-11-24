import sys,os,json
from PIL import Image,ImageDraw
names=sys.argv[2:]; out=sys.argv[1]
W,H=240,160; cols=3  # 3 roms per row, 2 pics each
rows=(len(names)+cols-1)//cols
S=Image.new('RGB',(cols*(2*W+10),rows*(H+14)),(20,20,20)); d=ImageDraw.Draw(S)
for i,n in enumerate(names):
    dr=n.replace('+','_'); x=(i%cols)*(2*W+10); y=(i//cols)*(H+14)
    d.text((x+2,y+1),n,fill=(255,220,120))
    for k,f in enumerate((f'{dr}/pg.png',f'{dr}/pg_f01.png')):
        if os.path.exists(f): S.paste(Image.open(f).convert('RGB').crop((0,0,240,160)),(x+k*W,y+14))
S.save(out); print(S.size)
