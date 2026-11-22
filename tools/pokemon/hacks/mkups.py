import sys,zlib,os
def vlq(n):
    o=bytearray()
    while True:
        x=n&0x7f; n>>=7
        if n: o.append(x); n-=1
        else: o.append(0x80|x); break
    return bytes(o)
def mkups(src,dst):
    out=bytearray(b'UPS1')+vlq(len(src))+vlq(len(dst))
    n=max(len(src),len(dst)); i=0; last=0
    s=src+b'\0'*(n-len(src)); d=dst+b'\0'*(n-len(dst))
    while i<n:
        if s[i]==d[i]:
            # skip run of equal using find of next diff
            j=i
            # fast scan in chunks
            while j<n and s[j]==d[j]:
                if j%4096==0 and s[j:j+4096]==d[j:j+4096]: j+=4096; continue
                j+=1
            i=j; continue
        j=i
        while j<n and s[j]!=d[j]: j+=1
        out+=vlq(i-last)
        out+=bytes(a^b for a,b in zip(s[i:j],d[i:j]))+b'\0'
        last=j+1; i=j+1
    out+=zlib.crc32(src).to_bytes(4,'little')+zlib.crc32(dst).to_bytes(4,'little')
    out+=zlib.crc32(bytes(out)).to_bytes(4,'little')
    return bytes(out)
if __name__=='__main__':
    src=open(sys.argv[1],'rb').read(); dst=open(sys.argv[2],'rb').read()
    open(sys.argv[3],'wb').write(mkups(src,dst))
