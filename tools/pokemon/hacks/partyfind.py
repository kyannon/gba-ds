import sys,struct,collections
d=open(sys.argv[1],'rb').read()
cnt=collections.Counter()
for i in range(0,len(d)-3,4):
    v=struct.unpack_from('<I',d,i)[0]
    if 0x02000000<=v<0x02040000: cnt[v]+=1
c=[(a,cnt[a],cnt[a+0x258]) for a in cnt if cnt.get(a+0x258,0)>=10 and cnt[a]>=20]
for a,n,m in sorted(c,key=lambda t:-t[1])[:6]: print(hex(a),n,hex(a+0x258),m)
