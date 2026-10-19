"""Execute real Demo renderer/drivers on RV32 QEMU with a software bus model."""
from pathlib import Path
import subprocess,json,hashlib,os
root=Path(__file__).resolve().parents[1];out=root/'06_Verification/rv32';out.mkdir(exist_ok=True)
ef=Path(os.environ.get('EFINITY_HOME','F:/Efinity/2026.1'));sw=root/'08_Game/src';engine=root/'01_Engine/software';tests=root/'tests/game'
files=[tests/'start.S',tests/'rv32_main.c']+[sw/(n+'.c') for n in ('taisei_render','taisei_game','stage1','cirno_boss','taisei_rain')]+[engine/'src'/(n+'.c') for n in ('fs_blitter','fs_draw','fs_scanline','fs_sprite_trim')]
cmd=[ef/'toolchain/bin/riscv-none-elf-gcc.exe','-march=rv32im_zicsr_zifencei','-mabi=ilp32','-mcmodel=medany','-msmall-data-limit=0','-O2','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-Wno-missing-field-initializers','-ffreestanding','-fno-builtin','-nostdlib','-DFS_DRIVER_TEST','-DSH_OFFLINE_TEST','-I',sw,'-I',engine/'include','-I',tests,'-T',tests/'test.ld',*files,out/'assets.o','-lgcc','-o',out/'tests.elf']
import shutil
shutil.copy2(root/'03_Assets/game_assets.bin',out/'assets.bin')
subprocess.run([str(ef/'toolchain/bin/riscv-none-elf-objcopy.exe'),'-I','binary','-O','elf32-littleriscv','-B','riscv','assets.bin','assets.o'],cwd=out,check=True)
p=subprocess.run(list(map(str,cmd)),capture_output=True,timeout=90);(out/'build.log').write_bytes(p.stdout+p.stderr)
if p.returncode:raise RuntimeError((p.stdout+p.stderr).decode(errors='replace'))
p=subprocess.run([str(ef/'qemu/qemu-system-riscv32.exe'),'-machine','virt','-m','32M','-bios','none','-kernel','tests.elf','-display','none','-monitor','none','-serial','stdio','-no-reboot'],cwd=out,capture_output=True,timeout=180)
log=p.stdout+p.stderr;(out/'test.log').write_bytes(log)
passed=p.returncode==0 and b'TAISEI_LUNATIC_RV32_PASS' in log
(out/'result.json').write_text(json.dumps(dict(passed=passed,board_accessed=False,source_sha256={str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() for p in files}),indent=2))
text=log.decode(errors='replace')
if 'FRAME_HEX_BEGIN' in text:
    from PIL import Image
    import struct
    raw=text.split('FRAME_HEX_BEGIN\n')[1].split('\nFRAME_HEX_END')[0];values=[int(raw[i:i+4],16) for i in range(0,len(raw),4)]
    image=Image.new('RGB',(640,480));image.putdata([(((v>>11)&31)*255//31,((v>>5)&63)*255//63,(v&31)*255//31) for v in values]);image.save(out/'preview.png')
    text=text.split('FRAME_HEX_BEGIN')[0]+text.split('FRAME_HEX_END')[1]
print(text);raise SystemExit(0 if passed else 1)
