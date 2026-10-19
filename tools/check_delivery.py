"""Offline delivery integrity and documentation check; never accesses a board."""
from pathlib import Path
import argparse
import hashlib
import json
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
HASH_FILE = ROOT / 'FILES_SHA256.json'
DOCS = ['README.md', '01_Engine/README.md', '07_Demo/README.md', '08_Game/README.md']


def delivery_files():
    for path in sorted(ROOT.rglob('*')):
        if not path.is_file() or path == HASH_FILE:
            continue
        relative = path.relative_to(ROOT)
        if any(part in ('.git', '.local', 'build', 'outflow', '__pycache__', '.metadata')
               for part in relative.parts):
            continue
        yield path


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def slug(heading):
    return ''.join(char for char in heading.lower() if char.isalnum() or char in '_- ').replace(' ', '-')


def check_links():
    for name in DOCS:
        path = ROOT / name
        text = path.read_text(encoding='utf-8')
        assert len(re.findall(r'^```', text, re.M)) % 2 == 0, name + ': unclosed code fence'
        for target in re.findall(r'\]\(([^)]+)\)', text):
            if target.startswith(('http:', 'https:', 'mailto:')):
                continue
            filename, marker, anchor = target.partition('#')
            linked = path.parent / filename if filename else path
            assert linked.exists(), name + ': missing link ' + target
            if marker and anchor:
                headings = re.findall(r'^#{1,6} (.+)$', linked.read_text(encoding='utf-8'), re.M)
                assert anchor in {slug(h) for h in headings}, name + ': missing anchor ' + target


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--refresh-hashes', action='store_true', help='Replace FILES_SHA256.json with current file hashes')
    args = parser.parse_args()
    files = list(delivery_files())
    for path in files:
        assert path.stat().st_size < 100 * 1024 * 1024, 'GitHub single-file limit: ' + str(path)
    check_links()
    assert not (ROOT / 'tools/ram_game.py').exists()
    assert not list(ROOT.rglob('*.launch')), 'Standalone RAM launch configuration found'
    subprocess.run([sys.executable, '-B', str(ROOT / 'tools/build_suite.py'), '--check'], check=True)
    current = {path.relative_to(ROOT).as_posix(): digest(path) for path in files}
    if args.refresh_hashes:
        HASH_FILE.write_text(json.dumps({'format': 1, 'algorithm': 'SHA256',
                                        'distribution': 'flash-only', 'files': current},
                                       ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
        print('DELIVERY_HASH_LIST_REFRESHED', len(current))
    else:
        expected = json.loads(HASH_FILE.read_text(encoding='utf-8'))['files']
        differences = sorted(name for name in set(expected) | set(current)
                             if expected.get(name) != current.get(name))
        assert not differences, 'Delivery file changes: ' + ', '.join(differences[:20])
        print('DELIVERY_FILES_LINKS_AND_FLASH_PASS', len(current))


if __name__ == '__main__':
    main()
