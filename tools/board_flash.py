"""Authorized staged Flash installation: backup, verified write, Flash CPU boot.
Run only after inspecting 04_Firmware/manifest.json. Restorable full backup retained."""
from pathlib import Path
import argparse,ctypes,hashlib,json,os,re,shutil,subprocess,sys,time,tempfile
R=Path(__file__).resolve().parents[1];EF=Path(os.environ.get('EFINITY_HOME','F:/Efinity/2026.1'));F=R/'04_Firmware';V=R/'.local/flash_install';W=Path(tempfile.gettempdir())/'FrameSpeed_Flash_Work'
def env():
 e=os.environ.copy();e.update(EFINITY_HOME=EF.as_posix(),EFXPGM_HOME=(EF/'pgm').as_posix(),EFXDBG_HOME=(EF/'debugger').as_posix(),PYTHONHOME=str(EF/'python311'),PYTHONIOENCODING='utf-8',PYTHONDONTWRITEBYTECODE='1',EFINITY_USER_DIR_INI=str(Path(e['LOCALAPPDATA'])/'efinity/user_dir.ini'))
 e['PATH']=';'.join(str(EF/p) for p in ['python311/bin','bin','pgm/bin','debugger/bin','openocd/bin'])+';'+e.get('PATH','');e['PYTHONPATH']=';'.join(str(EF/p) for p in ['pgm/bin','debugger/bin']);return e
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
def save(n,x):(V/(n+'.json')).write_text(json.dumps(x,ensure_ascii=False,indent=2),encoding='utf-8')
def run(cmd,name,timeout=600):
 with (V/(name+'.log')).open('wb') as log:p=subprocess.run(list(map(str,cmd)),env=env(),stdout=log,stderr=subprocess.STDOUT,timeout=timeout)
 text=(V/(name+'.log')).read_text(encoding='utf-8',errors='replace');print(text[-2400:],flush=True)
 if p.returncode:raise RuntimeError(name+' failed')
 return text
def main():
 ap=argparse.ArgumentParser();ap.add_argument('step',choices=['probe','backup','program','boot_test']);ap.add_argument('--port',default='COM6');a=ap.parse_args()
 if Path(sys.executable).resolve()!=(EF/'python311/bin/python.exe').resolve() or not os.environ.get('EFXDBG_HOME'):
  raise SystemExit(subprocess.run([str(EF/'python311/bin/python.exe'),'-B',str(Path(__file__).resolve()),a.step,'--port',a.port],env=env()).returncode)
 sys.stdout.reconfigure(encoding='utf-8',errors='replace');os.environ.update(env());sys.path[:0]=[str(EF/'pgm/bin'),str(EF/'debugger/bin')]
 from efx_pgm.usb_resolver import UsbResolver
 from efx_pgm.util.gen_util import set_efinity_user_dir_env
 from serial.tools import list_ports
 import serial
 set_efinity_user_dir_env();V.mkdir(parents=True,exist_ok=True);W.mkdir(parents=True,exist_ok=True)
 m=json.loads((F/'manifest.json').read_text(encoding="utf-8"));assert m['fpga_scanline_id']=='0x46534c06' and m['flash_decoded_bytes']==0x39f000
 assert sha(F/'FrameSpeed_Suite_flash.hex')==m['combined_sha256']
 for n,d in m['files_sha256'].items():assert sha(R/n)==d,n
 boards=[b for b in UsbResolver().get_usb_connections() if (b.vendor_id,b.product_id)==(0x0403,0x6011)]
 assert len(boards)==1 and boards[0].URLS[1];url=boards[0].URLS[1]
 assert any(p.device==a.port and (p.vid,p.pid)==(0x0403,0x6011) for p in list_ports.comports())
 cli=[EF/'python311/bin/python.exe','-B',EF/'pgm/bin/efx_pgm/ftdi_program.py']
 def read(size,name):
  path=W/(name+'.hex');text=run(cli+['-m','jtag_bridge','-u',url,'--jtag_bridge_mode','read','--address','0','--num_bytes',hex(size),'-o',path],name)
  data=bytes.fromhex(path.read_text(encoding="utf-8"));assert len(data)==size;return data,text
 ctypes.windll.kernel32.SetThreadExecutionState(0x80000003)
 try:
  if a.step=='probe':
   text=run([EF/'python311/bin/python.exe',EF/'pgm/bin/jtag_chain_detect.py','-f',url],'01_jtag');assert '0x10660a79' in text.lower()
   run(cli+[EF/'pgm/fli/titanium/u10660A79.bit','-m','jtag','-u',url],'02_bridge')
   data,text=read(256,'03_flash_probe');assert '0xEF6017' in text and '8 MiB' in text
   save('probe',dict(passed=True,url=url,idcode='0x10660a79',flash='0xEF6017',bytes=0x800000,flash_written=False));print('FLASH_PROBE_PASS',flush=True)
  elif a.step=='backup':
   assert json.loads((V/'probe.json').read_text(encoding="utf-8"))['url']==url
   backup=R/'.local/backups/original_flash_8m.bin';backup=backup.resolve();backup.parent.mkdir(parents=True,exist_ok=True)
   if backup.exists():backup=backup.with_name('flash_8m_'+time.strftime('%Y%m%d_%H%M%S')+'.bin')
   assert not backup.exists(),'Refuse to overwrite an existing backup'
   data,text=read(0x800000,'04_backup');backup.write_bytes(data);save('backup',dict(passed=True,path=str(backup),bytes=len(data),sha256=sha(backup),url=url,flash_written=False));print('FLASH_BACKUP_PASS',flush=True)
  elif a.step=='program':
   backup=json.loads((V/'backup.json').read_text(encoding="utf-8"));assert backup['url']==url and sha(Path(backup['path']))==backup['sha256']
   image=F/'FrameSpeed_Suite_flash.hex';local=W/'suite.hex';shutil.copy2(image,local)
   text=run(cli+[local,'-m','jtag_bridge','-u',url,'--address','0','--jtag_bridge_mode','all','--verify_method','hostx1'],'05_program_verify')
   assert 'Flash verify successful' in text
   data,text=read(m['flash_decoded_bytes'],'06_full_readback');expected=bytes.fromhex(image.read_text(encoding="utf-8"));assert data==expected
   save('program',dict(passed=True,flash_written=True,vendor_verify=True,full_readback_match=True,combined_sha256=m['combined_sha256'],decoded_bytes=len(data),backup=backup['path']));print('FLASH_WRITE_FULL_READBACK_PASS',flush=True)
  elif a.step=='boot_test':
   assert json.loads((V/'program.json').read_text(encoding="utf-8"))['combined_sha256']==m['combined_sha256']
   lines=[];pending=b''
   with serial.Serial(a.port,115200,timeout=.05,write_timeout=2) as uart:
    uart.reset_input_buffer();run(cli+[F/'components/FrameSpeed.bit','-m','jtag','-u',url],'07_fpga_config_only')
    def capture_until(token,seconds=180):
     nonlocal pending
     start=time.monotonic();capture=[]
     while time.monotonic()-start<seconds:
      pending+=uart.read(4096)
      while b'\n' in pending:
       raw,pending=pending.split(b'\n',1);line=raw.decode(errors='replace').strip();lines.append(line);capture.append(line)
       if 'DISPLAY_FAIL' in line:raise RuntimeError(line)
       if token in line:return capture
     raise RuntimeError('Missing '+token)
    startup=capture_until('DISPLAY_READY');assert any('SUITE_STAGING_CRC_OK' in s for s in startup)
    assert any('APP_MODE mode=MENU' in s for s in startup)
    uart.write(b'1');demo=capture_until('DISPLAY_READY');assert any('DEMO_PIXEL_MATCH' in s for s in demo) and any('DEMO_OVERLAP_PIXEL_MATCH' in s for s in demo)
    uart.write(b'm');menu=capture_until('DISPLAY_READY',30);assert any('APP_MODE mode=MENU' in s for s in menu)
    uart.write(b'2');game=capture_until('DISPLAY_READY');assert any('GAME_PIXEL_MATCH' in s for s in game) and any('GAME_ASSETS_CRC_OK' in s for s in game)
    uart.write(b'm');returned=capture_until('DISPLAY_READY',30);assert any('APP_MODE mode=MENU' in s for s in returned)
   (V/'boot_uart.log').write_text('\n'.join(lines),encoding='utf-8')
   save('boot',dict(passed=True,cpu_and_assets_loaded_from_flash=True,cpu_download_performed=False,fpga_configuration_loaded_via_jtag=True,power_cycle_cold_boot_verified=False,menu_demo_game_menu_pass=True,demo_pixel_check=True,game_pixel_check=True,screen_visually_verified=False))
   print('FLASH_BOOT_MENU_DEMO_GAME_MENU_PASS',flush=True)
 finally:ctypes.windll.kernel32.SetThreadExecutionState(0x80000000)
if __name__=='__main__':main()
