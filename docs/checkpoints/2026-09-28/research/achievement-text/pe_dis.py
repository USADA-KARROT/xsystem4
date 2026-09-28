import struct, pathlib, sys, capstone, os
p=pathlib.Path(os.environ['XS4_EXE_DUMP'])
b=p.read_bytes(); nt=struct.unpack_from('<I',b,0x3c)[0]; opt=nt+24
base=struct.unpack_from('<I',b,opt+28)[0]; n=struct.unpack_from('<H',b,nt+6)[0]; sh=opt+struct.unpack_from('<H',b,nt+20)[0]
sections=[]
for i in range(n):
 q=sh+i*40; vs,va,rs,rp=struct.unpack_from('<IIII',b,q+8); sections.append((base+va,max(vs,rs),rp))
def read(va,n):
 for s,z,r in sections:
  if s<=va<s+z:return b[r+va-s:r+va-s+n]
 raise ValueError(hex(va))
def dis(va,n):
 for i in capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32).disasm(read(va,n),va):print(f'{i.address:08x}: {i.mnemonic:9} {i.op_str}')
if __name__=='__main__':
 if sys.argv[1]=='table':
  lines=pathlib.Path(os.environ['XS4_LIBRARIES_DUMP']).read_text().splitlines(); idx=lines.index('--- PartsEngine ---'); fun=[]
  for line in lines[idx+1:]:
   if line.startswith('---'):break
   if line:fun.append(line)
  for k,sig in enumerate(fun):
   if any(s in sig for s in sys.argv[2:]):print(k,sig,hex(struct.unpack('<I',read(0x589714+k*4,4))[0]))
 else:dis(int(sys.argv[1],0),int(sys.argv[2],0))
