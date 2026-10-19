"""Build the three official IDE projects and assemble the single Flash image.

No board connection, Flash write, or standalone RAM loading is performed.
Use --check for a read-only consistency check of the shipped image/components.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import shutil
import struct
import subprocess
import tempfile
import zlib

ROOT = Path(__file__).resolve().parents[1]
EF = Path(os.environ.get('EFINITY_HOME', 'F:/Efinity/2026.1'))
FIRMWARE = ROOT / '04_Firmware'
COMPONENTS = FIRMWARE / 'components'
RECORDS = ROOT / '06_Verification'
APPS = [('02_App', 'FrameSpeed_Release_Menu', 'menu'),
        ('07_Demo', 'FrameSpeed_Release_Demo', 'demo'),
        ('08_Game', 'FrameSpeed_Release_Game', 'game')]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def verify_hardware():
    record = json.loads((RECORDS / 'reference/game/hardware.json').read_text(encoding='utf-8'))
    assert sha(COMPONENTS / 'FrameSpeed.bit') == record['bit_sha256'], 'FPGA bitstream mismatch'
    for name, digest in record['source_sha256'].items():
        # The provenance record uses Windows separators; normalize for inspection.
        path = ROOT / name.replace('\\', '/')
        assert sha(path) == digest, 'RTL/IP configuration changed: ' + name


def compile_apps():
    env = os.environ.copy()
    env.update(EFINITY_HOME=str(EF), PYTHONIOENCODING='utf-8', PYTHONDONTWRITEBYTECODE='1')
    env['PATH'] = ';'.join(str(EF / p) for p in
                           ['build_tools/bin', 'toolchain/bin', 'python311/bin']) + ';' + env.get('PATH', '')
    temp_parent = Path(os.environ.get('FRAMESPEED_TEMP_ROOT', tempfile.gettempdir())).resolve()
    temp_parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='framespeed_flash_ide_', dir=temp_parent) as folder:
        temp = Path(folder).resolve()
        assert temp.parent == temp_parent and str(temp).isascii(), 'Use an ASCII temporary path'
        stage = temp / 'release'
        # C compilation needs BSP/drivers/apps/assets, not the generated FPGA IP
        # tree. Keeping that tree out also avoids Windows long-path failures.
        stage.mkdir()
        for directory in ('01_Engine', '02_App', '03_Assets', '07_Demo', '08_Game', 'shared'):
            shutil.copytree(ROOT / directory, stage / directory,
                            ignore=shutil.ignore_patterns('fpga', '__pycache__', '*.pyc', 'build'))
        # Keep the IDE configuration writable without changing its installation
        # or relying on a pre-existing configuration in another user's home.
        configuration = temp / 'configuration'
        for relative in ('config.ini', 'org.eclipse.equinox.simpleconfigurator/bundles.info'):
            target = configuration / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(EF / 'Efinity-RISCV-IDE/configuration' / relative, target)
        for app, project, name in APPS:
            command = [EF / 'Efinity-RISCV-IDE/efinity-riscv-idec.exe', '-nosplash',
                       '--launcher.suppressErrors', '-configuration', configuration,
                       '-data', temp / 'workspace',
                       '-application', 'org.eclipse.cdt.managedbuilder.core.headlessbuild',
                       '-import', stage / app, '-cleanBuild', project]
            result = subprocess.run(list(map(str, command)), cwd=stage, env=env,
                                    capture_output=True, timeout=240)
            log = result.stdout + result.stderr
            (RECORDS / ('build_' + name + '.log')).write_bytes(log)
            assert result.returncode == 0 and b'Build Finished. 0 errors' in log, log[-5000:]
            if b'Cannot run program' in log or not (stage / app / 'build/framespeed.elf').exists():
                raise RuntimeError('IDE could not execute the build tools: ' +
                                   log[-5000:].decode('utf-8', errors='replace'))
            output = ROOT / app / 'build'
            output.mkdir(exist_ok=True)
            for extension in ('elf', 'bin', 'hex', 'map'):
                shutil.copy2(stage / app / 'build' / ('framespeed.' + extension),
                             output / ('framespeed.' + extension))
            shutil.copy2(output / 'framespeed.bin', COMPONENTS / (name + '.bin'))
            print('OFFICIAL_IDE_BUILD_PASS', name, flush=True)
    (RECORDS / 'official_ide_build.json').write_text(json.dumps({
        'passed': True, 'apps': [name for _, _, name in APPS],
        'flash_written': False, 'output': 'Flash image components'}, indent=2), encoding='utf-8')


def assemble():
    configuration = bytes.fromhex((COMPONENTS / 'fpga.hex').read_text(encoding='ascii'))
    assert 0 < len(configuration) < 0xE0000
    programs = []
    for name in ('menu', 'demo', 'game'):
        data = (COMPONENTS / (name + '.bin')).read_bytes()
        assert 0 < len(data) <= 0x1F000, name + ': CPU RAM capacity exceeded'
        programs.append(data.ljust(0x1F000, b'\xff'))
    demo_background = (ROOT / '03_Assets/background_store.bin').read_bytes()
    game_assets = (ROOT / '03_Assets/game_assets.bin').read_bytes()
    triples = [(0x380000, programs[0]), (0xE1000, programs[1]), (0x100000, programs[2]),
               (0x290000, demo_background), (0x120000, game_assets)]
    header = [0x46535549, 1, 0x26101851]
    for address, data in triples:
        header.extend((address, len(data), zlib.crc32(data)))
    header_bytes = struct.pack('<18I', *header)
    regions = [('FPGA', 0, configuration), ('Suite header', 0xE0000, header_bytes)]
    for name, (address, data) in zip(('Menu', 'Demo', 'Game', 'Demo background', 'Game assets'), triples):
        regions.append((name, address, data))
    end = 0
    for name, address, data in sorted(regions, key=lambda item: item[1]):
        assert address >= end, name + ': Flash regions overlap'
        end = address + len(data)
    assert end == 0x39F000, 'Update the format, loader and capacity checks together'
    image = bytearray(b'\xff' * end)
    for _, address, data in regions:
        image[address:address + len(data)] = data
    return bytes(image), header_bytes, regions


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument('--check', action='store_true', help='Read-only image/components/hash verification')
    mode.add_argument('--pack-only', action='store_true', help='Repackage existing CPU components without compiling')
    args = parser.parse_args()
    verify_hardware()
    if not (args.check or args.pack_only):
        compile_apps()
    image, header, regions = assemble()
    destination = FIRMWARE / 'FrameSpeed_Suite_flash.hex'
    if args.check:
        manifest = json.loads((FIRMWARE / 'manifest.json').read_text(encoding='utf-8'))
        assert bytes.fromhex(destination.read_text(encoding='ascii')) == image
        assert (COMPONENTS / 'suite_header.bin').read_bytes() == header
        assert sha(destination) == manifest['combined_sha256']
        for name, digest in manifest['files_sha256'].items():
            assert sha(ROOT / name) == digest, name
        print('FLASH_IMAGE_COMPONENTS_AND_HASHES_PASS', len(image), sha(destination))
        return
    (COMPONENTS / 'suite_header.bin').write_bytes(header)
    # The vendor image is raw hexadecimal byte text, not Intel HEX.
    # Preserve the checked vendor-format file byte-for-byte across platforms.
    destination.write_bytes(''.join('%02x\r\n' % byte for byte in image).encode('ascii'))
    manifest = dict(version='49_SUITE_ALPHA50', distribution='flash-only',
                    applications=['Menu', 'Demo', 'Taisei Stage1'], fpga_scanline_id='0x46534c06',
                    fpga_changed=False, flash_written=False, cold_boot_verified=False,
                    flash_decoded_bytes=len(image), required_flash_capacity=0x400000,
                    physical_flash_expected=0x800000, combined_sha256=sha(destination),
                    hardware_bit_sha256=sha(COMPONENTS / 'FrameSpeed.bit'),
                    regions=[dict(name=name, address=address, bytes=len(data), crc32=zlib.crc32(data))
                             for name, address, data in regions],
                    game_alpha=dict(player_shot=128, power=128, point=128, trails=[80, 32]),
                    files_sha256={})
    for path in [*sorted(COMPONENTS.iterdir()), ROOT / '03_Assets/game_assets.bin',
                 ROOT / '03_Assets/background_store.bin']:
        if path.is_file():
            manifest['files_sha256'][path.relative_to(ROOT).as_posix()] = sha(path)
    (FIRMWARE / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    print('SUITE_FLASH_IMAGE_READY', len(image), manifest['combined_sha256'], flush=True)


if __name__ == '__main__':
    main()
