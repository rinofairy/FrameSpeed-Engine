"""Execute real Demo renderer/drivers on RV32 QEMU with a software bus model."""
from pathlib import Path
import subprocess,json,hashlib,os
root=Path(__file__).resolve().parents[1];out=root/'06_Verification/rv32_demo';out.mkdir(exist_ok=True)
ef=Path(os.environ.get('EFINITY_HOME','F:/Efinity/2026.1'));sw=root/'07_Demo/src';engine=root/'01_Engine/software';tests=root/'tests/demo'
files=[tests/'start.S',tests/'rv32_main.c']+[sw/(n+'.c') for n in ('taisei_render','suite_demo','demo_control','sh_assets','sh_background')]+[engine/'src'/(n+'.c') for n in ('fs_blitter','fs_draw','fs_scanline','fs_sprite_trim')]
cmd=[ef/'toolchain/bin/riscv-none-elf-gcc.exe','-march=rv32im_zicsr_zifencei','-mabi=ilp32','-mcmodel=medany','-msmall-data-limit=0','-O2','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-Wno-missing-field-initializers','-ffreestanding','-fno-builtin','-nostdlib','-DFS_DRIVER_TEST','-DSH_OFFLINE_TEST','-DFS_DEMO_COMPACT','-I',sw,'-I',engine/'include','-I',tests,'-T',tests/'test.ld',*files,'-lgcc','-o',out/'tests.elf']
p=subprocess.run(list(map(str,cmd)),capture_output=True,timeout=90);(out/'build.log').write_bytes(p.stdout+p.stderr)
if p.returncode:raise RuntimeError((p.stdout+p.stderr).decode(errors='replace'))
p=subprocess.run([str(ef/'qemu/qemu-system-riscv32.exe'),'-machine','virt','-m','32M','-bios','none','-kernel','tests.elf','-display','none','-monitor','none','-serial','stdio','-no-reboot'],cwd=out,capture_output=True,timeout=180)
log=p.stdout+p.stderr;(out/'test.log').write_bytes(log)
passed=p.returncode==0 and b'DEMO_OVERLAP_RV32_PASS' in log
(out/'result.json').write_text(json.dumps(dict(passed=passed,board_accessed=False,source_sha256={str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [*files,tests/'demo_cases.h',tests/'trim_cases.h']}),indent=2))
print(log.decode(errors='replace'));raise SystemExit(0 if passed else 1)
