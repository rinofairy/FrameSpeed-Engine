"""Decode and execute the actual DDR trampoline bytes; model memory guards.
Models RV32 copy/control semantics, not Sapphire caches, timing or hardware."""
from pathlib import Path
import subprocess,re,struct,json,hashlib,os
R=Path(__file__).resolve().parents[1];F=R/'04_Firmware/components';EF=Path(os.environ.get('EFINITY_HOME','F:/Efinity/2026.1'))
p=subprocess.run([str(EF/'toolchain/bin/riscv-none-elf-nm.exe'),str(R/'02_App/build/framespeed.elf')],capture_output=True,check=True)
symbols={n:int(a,16) for a,_,n in (s.split() for s in p.stdout.decode().splitlines() if len(s.split())==3)}
start=symbols['_binary_chain_bin_start'];end=symbols['_binary_chain_bin_end'];code=(F/'menu.bin').read_bytes()[start-0x1000:end-0x1000];assert 0<len(code)<4096
sx=lambda x,b:x-(1<<b) if x&(1<<(b-1)) else x
results=[]
for app in range(4):
 regions=[(0,bytearray(b'\xa5'*0x21000)),(0x1000000,bytearray(b'\xa5'*0x680000)),(0x470000,bytearray(b'\xa5'*0x180000)),(0x730000,bytearray(b'\xa5'*0x100000))]
 def locate(a,n):
  for base,data in regions:
   if base<=a and a+n<=base+len(data):return data,a-base
  raise AssertionError(('unmapped',hex(a)))
 def put(a,data):m,k=locate(a,len(data));m[k:k+len(data)]=data
 def word(a):m,k=locate(a,4);return struct.unpack_from('<I',m,k)[0]
 def store(a,v):
  assert 0x1000<=a<0x20000 or 0x47ffc0<=a<0x47ffc0+len(game) or 0x73ffc0<=a<0x73ffc0+len(bg),('illegal overwrite',hex(a))
  put(a,struct.pack('<I',v&0xffffffff))
 game=(R/'03_Assets/game_assets.bin').read_bytes();bg=(R/'03_Assets/background_store.bin').read_bytes()
 programs=[(F/(n+'.bin')).read_bytes().ljust(0x1f000,b'\xff') for n in ['menu','demo','game']]
 for i,data in enumerate(programs):put(0x1100000+(i<<17),data)
 put(0x1400000,bg);put(0x1500000,game);put(0x1010000,(F/'suite_header.bin').read_bytes());put(0x1000000,code)
 reg=[0]*32;reg[10]=app;reg[2]=0x1ff00;reg[3]=0x12345678;csr={0x300:8};pc=0x1000000;steps=0;writes=0
 while steps<(200 if app==3 else 4000000):
  ins=word(pc);pc+=4;steps+=1;op=ins&127;rd=ins>>7&31;f=ins>>12&7;rs1=ins>>15&31;rs2=ins>>20&31;value=None
  if op==0x37:value=ins&0xfffff000
  elif op==0x13:value=(reg[rs1]+sx(ins>>20,12)) if f==0 else reg[rs1]<<(ins>>20&31)
  elif op==0x33:value=reg[rs1]+reg[rs2];assert f==0
  elif op==3:value=word((reg[rs1]+sx(ins>>20,12))&0xffffffff);assert f==2
  elif op==0x23:
   imm=(ins>>25<<5)|(ins>>7&31);store((reg[rs1]+sx(imm,12))&0xffffffff,reg[rs2]);writes+=1;assert f==2
  elif op==0x63:
   imm=((ins>>31)<<12)|((ins>>7&1)<<11)|((ins>>25&63)<<5)|((ins>>8&15)<<1)
   yes=reg[rs1]==reg[rs2] if f==0 else reg[rs1]!=reg[rs2] if f==1 else reg[rs1]<reg[rs2] if f==6 else reg[rs1]>=reg[rs2];assert f in (0,1,6,7)
   if yes:pc=pc-4+sx(imm,13)
  elif op==0x6f:
   imm=((ins>>31)<<20)|((ins>>12&255)<<12)|((ins>>20&1)<<11)|((ins>>21&1023)<<1);value=pc;pc=pc-4+sx(imm,21)
  elif op==0x67:value=pc;pc=(reg[rs1]+sx(ins>>20,12))&~1
  elif op==0x73:
   key=ins>>20;old=csr.get(key,0);csr[key]=reg[rs1] if f==1 else old&~rs1;value=old;assert f in (1,7)
  elif op==0x0f:pass
  else:raise AssertionError(hex(ins))
  if rd and value is not None:reg[rd]=value&0xffffffff
  reg[0]=0
  if pc==0x1000:break
 if app==3:assert writes==0 and pc!=0x1000
 else:
  assert pc==0x1000 and csr[0x340]==0x4653574d and not(csr[0x300]&8)
  m,k=locate(0x1000,0x1f000);assert m[k:k+0x1f000]==programs[app] and m[:0x1000]==b'\xa5'*0x1000 and m[0x20000:]==b'\xa5'*0x1000
  if app:addr,data=(0x73ffc0,bg) if app==1 else (0x47ffc0,game);m,k=locate(addr,len(data));assert m[k:k+len(data)]==data and m[k-4:k]==b'\xa5'*4 and m[k+len(data):k+len(data)+4]==b'\xa5'*4
  assert reg[2]==0x1ff00 and reg[3]==0x12345678
 results.append(dict(app=app,instructions=steps,writes=writes))
(R/'06_Verification/switcher_model.json').write_text(json.dumps(dict(passed=True,actual_code_sha256=hashlib.sha256(code).hexdigest(),cache_or_timing_verified=False,cases=results),indent=2));print('DDR_SWITCHER_INSTRUCTION_AND_MEMORY_GUARDS_PASS',results)
