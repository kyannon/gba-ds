import sys,struct,collections,subprocess,os
H='/home/claude/gba-ds/build/host/harness'
rom=sys.argv[1]; d=rom[:-4].replace('+','_')
rd=open(rom,'rb').read()
cnt=collections.Counter()
for i in range(0,len(rd)-3,4):
    v=struct.unpack_from('<I',rd,i)[0]
    if 0x03000000<=v<0x03008000: cnt[v]+=1
open(f'{d}/ds.txt','w').write("load s_new\ndumpram sb\n")
subprocess.run([H,os.path.abspath(rom),d,f'{d}/ds.txt'],capture_output=True)
ram=open(f'{d}/sb.ram','rb').read()
out=[]
for a,n in cnt.most_common(60):
    o=0x40000+(a-0x03000000)
    p=struct.unpack_from('<I',ram,o)[0]
    if 0x02000000<=p<0x02040000: out.append((a,n,p))
for a,n,p in out[:10]: print(hex(a),n,hex(p))
