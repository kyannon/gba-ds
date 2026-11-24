import sys,struct
d=open(sys.argv[1],'rb').read()
n=len(d)
def addr(o): return 0x02000000+o if o<0x40000 else 0x03000000+(o-0x40000)
out=[]
for o in range(0,n-40,4):
    ok=True; ps=[]
    for k in range(5):
        p,c=struct.unpack_from('<II',d,o+8*k)
        if not (0x02000000<=p<0x02040000) or not (1<=c<=0x1000) : ok=False;break
        ps.append((p,c))
    if ok:
        out.append((addr(o),ps))
for a,ps in out: print(hex(a),[(hex(p),c) for p,c in ps])
