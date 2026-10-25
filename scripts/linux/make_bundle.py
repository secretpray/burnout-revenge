#!/usr/bin/env python3
"""Package a locally built Linux candidate; never bundle glibc or GPU drivers."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import tarfile
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def run(*args):
    return subprocess.check_output(args, text=True, stderr=subprocess.STDOUT)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('build', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--deps', type=Path, required=True)
    parser.add_argument('--allow-unverified', action='store_true', help='Internal RC only; marks hardware gates open')
    args = parser.parse_args()
    if platform.system() != 'Linux':
        parser.error('Run on Linux, with a native ELF build')
    if not args.allow_unverified:
        parser.error('No hardware release qualification recorded: use --allow-unverified for an internal RC')
    build, deps, output = args.build.resolve(), args.deps.resolve(), args.output.resolve()
    binary = build / 'runtime/BurnoutRevenge'
    machine = run('readelf', '-h', str(binary))
    arch = 'x86_64' if 'Advanced Micro Devices X86-64' in machine else 'arm64' if 'AArch64' in machine else None
    if not arch:
        parser.error('Unsupported ELF machine')
    info = (build / 'runtime/generated/build_info.inc').read_text()
    def define(key):
        match = re.search(r'^#define BURNOUT_BUILD_' + key + r' (.+)$', info, re.M)
        return match.group(1).strip('"') if match else 'unknown'
    version, commit = define('VERSION'), define('COMMIT')
    if commit == 'unknown' or define('DIRTY') != '0':
        parser.error('Candidate must come from a clean, identified revision')
    output.mkdir(parents=True, exist_ok=True)
    name = f'BurnoutRevenge-{version}-{commit}-linux-{arch}-rc'
    archive = output / (name + '.tar.xz')
    if archive.exists():
        parser.error(f'Refusing to overwrite {archive}')
    with tempfile.TemporaryDirectory(prefix='burnout-package-') as temp:
        stage = Path(temp) / 'BurnoutRevenge'
        lib = stage / 'lib'
        lib.mkdir(parents=True)
        licenses = stage / 'licenses'
        licenses.mkdir()
        symbols = output / 'symbols'
        symbols.mkdir(exist_ok=True)
        for exe in ('BurnoutRevenge', 'burnout-lobby'):
            source = build / 'runtime' / exe
            debug = symbols / f'{name}-{exe}.debug'
            run('objcopy', '--only-keep-debug', str(source), str(debug))
            run('objcopy', '--strip-debug', f'--add-gnu-debuglink={debug}', str(source), str(lib / exe))
        env = os.environ.copy()
        env['LD_LIBRARY_PATH'] = str(deps / 'lib')
        libraries = subprocess.check_output(['ldd', str(binary)], text=True, env=env)
        if 'not found' in libraries:
            parser.error('Unresolved dependencies: ' + libraries)
        bundled, system = [], []
        for line in libraries.splitlines():
            match = re.match(r'\s*(\S+) => (/\S+) ', line)
            if not match:
                continue
            soname, source = match.groups()
            # Display/audio/server libraries must match the host. C/C++ runtime
            # requirements are measured and documented instead of replacing glibc.
            if re.match(r'lib(c\.|m\.|dl\.|rt\.|pthread\.|stdc\+\+|gcc_s|vulkan|X|xcb|wayland|decor|drm|gbm|GL|EGL|pipewire|pulse|asound|udev|dbus|systemd)', soname):
                system.append(soname)
                continue
            shutil.copy2(source, lib / soname, follow_symlinks=True)
            bundled.append(soname)
            if Path(source).resolve().is_relative_to(deps):
                continue  # dependency prefix carries the source licenses below
            # Ubuntu SDK libraries: the package copyright is mandatory.
            owner = run('dpkg-query', '-S', source).split(': /', 1)[0].split(':')[0]
            copyright_file = Path('/usr/share/doc') / owner / 'copyright'
            if not copyright_file.is_file():
                parser.error(f'No license for bundled {soname}')
            shutil.copy2(copyright_file, licenses / (owner + '-copyright'))
        for path in lib.iterdir():
            dynamic = run('readelf', '-d', str(path))
            if '(RPATH)' in dynamic or '(RUNPATH)' in dynamic:
                run('patchelf', '--set-rpath', '$ORIGIN', str(path))
        for filename in ('COPYING', 'NOTICE', 'THIRD_PARTY_NOTICES.md', 'CREDITS.md'):
            shutil.copy2(ROOT / filename, licenses / filename)
        shutil.copytree(ROOT / 'LICENSES', licenses / 'LICENSES')
        shutil.copytree(deps / 'share/licenses', licenses / 'dependencies')
        shutil.copy2(deps / 'share/linux-dependencies.txt', licenses / 'linux-dependencies.txt')
        shutil.copy2(ROOT / 'scripts/linux/burnout-revenge.sh', stage)
        shutil.copy2(ROOT / 'scripts/linux/README.md', stage)
        shutil.copy2(ROOT / 'scripts/macos/AppIcon.png', stage / 'burnout-revenge.png')
        (stage / 'burnout-revenge.desktop').write_text(
            '[Desktop Entry]\nType=Application\nName=Burnout Revenge (Linux RC)\n'
            'Exec=burnout-revenge.sh\nIcon=burnout-revenge\nTerminal=false\nCategories=Game;\n')
        requirements = {}
        for prefix in ('GLIBC', 'GLIBCXX', 'CXXABI'):
            versions = []
            for path in lib.iterdir():
                text = run('readelf', '--version-info', str(path))
                needed = text.split('Version needs section', 1)[-1] if 'Version needs section' in text else ''
                versions += re.findall(r'Name: ' + prefix + r'_(\d+(?:\.\d+)+)', needed)
            requirements[prefix] = max(versions, key=lambda v: tuple(map(int, v.split('.'))), default='none')
        (stage / 'ARCHITECTURE').write_text(('x86_64' if arch == 'x86_64' else 'aarch64') + '\n')
        (stage / 'MIN_GLIBC').write_text(requirements['GLIBC'] + '\n')
        manifest = dict(version=version, revision=commit, architecture=arch,
                        qualification='UNVERIFIED RELEASE CANDIDATE; native GPU gates OPEN',
                        required_symbol_versions=requirements, bundled_libraries=sorted(bundled),
                        system_libraries=sorted(system))
        (stage / 'bundle-info.json').write_text(json.dumps(manifest, indent=2) + '\n')
        forbidden = [str(ROOT).encode(), str(build).encode(), str(Path.home()).encode() + b'/']
        cache = (build / 'CMakeCache.txt').read_text()
        for key in ('CMAKE_HOME_DIRECTORY', 'FFMPEG_XMA', 'SDL3_DIR'):
            match = re.search(r'^' + key + r':[^=]*=(/.+)$', cache, re.M)
            if match:
                forbidden.append(match.group(1).encode())
        for path in lib.iterdir():
            data = path.read_bytes()
            if any(value in data for value in forbidden):
                parser.error(f'Build path leaked into {path.name}; rebuild with path mapping')
        sums = []
        for path in sorted(stage.rglob('*')):
            if path.is_file():
                sums.append(hashlib.sha256(path.read_bytes()).hexdigest() + '  ' + path.relative_to(stage).as_posix())
        (stage / 'SHA256SUMS').write_text('\n'.join(sums) + '\n')
        with tarfile.open(archive, 'w:xz') as tar:
            tar.add(stage, arcname=stage.name)
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    archive.with_suffix(archive.suffix + '.sha256').write_text(digest + '  ' + archive.name + '\n')
    print(json.dumps(manifest, indent=2))
    print(archive)


if __name__ == '__main__':
    main()
