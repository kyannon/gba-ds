import sys
from PIL import Image,ImageDraw
ds=sys.argv[2:]
names=['p1','pg','pg_f01']
im=Image.new('RGB',(240*3,(160+12)*len(ds)),'white');dr=ImageDraw.Draw(im)
for r,d in enumerate(ds):
    dr.text((2,r*172),d,fill='black')
    for c,n in enumerate(names):
        try:
            a=Image.open(f'{d}/{n}.png').convert('RGB')
            im.paste(a.crop((0,0,240,160)) if a.size[1]<=160 else a.resize((240,160)),(c*240,r*172+12))
        except Exception as e: pass
im.save(sys.argv[1])
