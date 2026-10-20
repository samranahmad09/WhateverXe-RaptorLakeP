#!/usr/bin/env python3
"""Package the OpenCore plugin and symbols; the graphics stack is supplied by WhateverXe Graphics."""
import argparse
import hashlib
import json
import plistlib
import re
import shutil
import subprocess
import tempfile
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def run(*args):
    return subprocess.check_output([str(x) for x in args], cwd=ROOT, text=True).strip()


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--configuration', choices=['DEBUG', 'RELEASE'], required=True)
    parser.add_argument('--output', type=Path, default=ROOT / 'build/dist')
    parser.add_argument('--tag', default='')
    args = parser.parse_args()
    build, output = args.build.resolve(), args.output.resolve()
    manifest = json.loads((build / 'build-manifest.json').read_text())['WhateverXe']
    source = build / 'WhateverXe.kext'
    source_exe = source / 'Contents/MacOS/WhateverXe'
    info = plistlib.loads((source / 'Contents/Info.plist').read_bytes())
    version = info['CFBundleVersion']
    assert re.fullmatch(r'\d+\.\d+\.\d+', version), 'invalid version'
    assert info['CFBundleIdentifier'] == 'org.b00t0x.WhateverXe'
    assert manifest['version'] == version and manifest['configuration'] == args.configuration
    assert sha(source_exe) == manifest['sha256'], 'build manifest mismatch'
    assert not args.tag or args.tag.removeprefix('v') == version, 'tag/version mismatch'
    assert run('xcrun', 'lipo', '-archs', source_exe) == 'x86_64'
    output.mkdir(parents=True, exist_ok=True)
    archive = output / f'WhateverXe-{version}-{args.configuration}.zip'
    with tempfile.TemporaryDirectory(prefix='wex-package-') as temporary:
        stage = Path(temporary)
        bundle = stage / source.name
        shutil.copytree(source, bundle)
        exe = bundle / 'Contents/MacOS/WhateverXe'
        symbols = stage / 'WhateverXe.kext.dSYM'
        shutil.copytree(build / 'WhateverXe.kext.dSYM', symbols)
        dwarf = symbols / 'Contents/Resources/DWARF/WhateverXe'
        assert 'DW_TAG_compile_unit' in run('xcrun', 'dwarfdump', '--debug-info', dwarf), 'missing DWARF information'
        for relocation in symbols.rglob('*.yml'):
            text = relocation.read_text()
            text = re.sub(r'(?m)^binary-path:.*$',
                          "binary-path: 'WhateverXe.kext/Contents/MacOS/WhateverXe'", text)
            relocation.write_text(text)
        # Keep symbols in dSYM and remove linker object-path records from the kext.
        run('xcrun', 'strip', '-S', exe)
        if args.configuration == 'RELEASE':
            run('xcrun', 'strip', '-x', exe)
        macho_uuid = re.search(r'UUID: ([A-F0-9-]+)', run('xcrun', 'dwarfdump', '--uuid', exe)).group(1)
        assert macho_uuid in run('xcrun', 'dwarfdump', '--uuid', symbols), 'symbol UUID mismatch'
        # Neither local account paths nor Apple driver inputs belong in release assets.
        for path in stage.rglob('*'):
            assert not path.is_symlink(), 'unexpected packaging symlink'
            if path.is_file():
                data = path.read_bytes()
                assert b'/Users/' not in data and b'/home/' not in data, 'build-machine path in artifact'
        with zipfile.ZipFile(archive, 'w', compression=zipfile.ZIP_DEFLATED) as package:
            for path in sorted(stage.rglob('*')):
                if path.is_file():
                    package.write(path, path.relative_to(stage))
        with zipfile.ZipFile(archive) as package:
            assert package.testzip() is None
            assert {name.split('/')[0] for name in package.namelist()} == {
                'WhateverXe.kext', 'WhateverXe.kext.dSYM'
            }, 'unexpected archive contents'
    print(json.dumps({'archive': archive.name, 'sha256': sha(archive), 'uuid': macho_uuid}))


if __name__ == '__main__':
    main()
