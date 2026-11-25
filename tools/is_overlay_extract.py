import sys,os
src,out=sys.argv[1],sys.argv[2]
d=open(src,'rb').read()
p=d.find(b'PADDINGXXPADDING')
while d[p:p+8] in (b'PADDINGX',b'XPADDING') : p+=8
# skip remaining padding bytes
while d[p:p+1] in b'PADINGX' and d[p:p+12]!=b'Autorun.exe\0' and not d[p:p+1]==b'': 
    if d[p:p+1] in b'PADINGX': p+=1
p=d.find(b'\0',p-64) if False else p
while p<len(d):
    f=[]
    for _ in range(4):
        e=d.index(b'\0',p); f.append(d[p:e].decode('latin1')); p=e+1
    name,path,ver,size=f; size=int(size)
    dst=os.path.join(out,path.replace('\\','/'))
    os.makedirs(os.path.dirname(dst),exist_ok=True)
    open(dst,'wb').write(d[p:p+size]); p+=size
    print(size,path,ver)
