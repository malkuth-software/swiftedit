"""Package tested Windows/Linux binaries with their non-system runtime closure."""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tarfile
import tempfile
import zipfile
from publication_version import project_version, verify_build_version
from installers import build_and_verify


def run(arguments: list[str], environment: dict[str, str] | None = None) -> str:
    result: subprocess.CompletedProcess[str] = subprocess.run(
        arguments, env=environment, text=True, stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT, check=True, timeout=60)
    return result.stdout


def digest(path: Path) -> str:
    with path.open('rb') as stream:
        result: str = hashlib.file_digest(stream, 'sha256').hexdigest()
    return result


def windows_libraries(executables: list[Path], destination: Path, sdk: Path) -> None:
    tool: str | None = shutil.which('objdump')
    if tool is None:
        raise RuntimeError('objdump is required for dependency inspection')
    system: Path = Path(os.environ['WINDIR']) / 'System32'
    directories: list[Path] = [executables[0].parent, sdk / 'gui-forms-sdk/bin',
                               sdk / 'picker-sdk/bin', Path(tool).parent, system]
    pending: list[Path] = list(executables)
    seen: set[str] = set()
    while pending:
        binary: Path = pending.pop()
        listing: str = run([tool, '-p', str(binary)])
        dependency: str
        for dependency in re.findall(r'DLL Name:\s*(\S+)', listing):
            key: str = dependency.casefold()
            if key in seen or key.startswith(('api-ms-win-', 'ext-ms-win-')):
                continue
            seen.add(key)
            found: Path | None = None
            directory: Path
            for directory in directories:
                candidate: Path = directory / dependency
                if candidate.is_file():
                    found = candidate.resolve()
                    break
            if found is None:
                raise RuntimeError('Unresolved Windows dependency: ' + dependency)
            if found.is_relative_to(system.resolve()):
                continue
            target: Path = destination / dependency
            if target.exists() and digest(target) != digest(found):
                raise RuntimeError('Conflicting runtime dependency: ' + dependency)
            shutil.copy2(found, target)
            pending.append(found)


def linux_libraries(executables: list[Path], destination: Path) -> None:
    baseline: set[str] = {'libc.so.6', 'libm.so.6', 'libdl.so.2', 'libpthread.so.0',
                          'librt.so.1', 'libresolv.so.2', 'libutil.so.1'}
    libraries: dict[str, Path] = {}
    binary: Path
    for binary in executables:
        listing: str = run(['ldd', str(binary)])
        if 'not found' in listing:
            raise RuntimeError('Unresolved Linux runtime dependency: ' + listing)
        line: str
        for line in listing.splitlines():
            match: re.Match[str] | None = re.search(r'^\s*(\S+) => (/\S+)', line)
            if match is None or match.group(1) in baseline:
                continue
            name: str = match.group(1)
            source: Path = Path(match.group(2)).resolve()
            if name in libraries and digest(libraries[name]) != digest(source):
                raise RuntimeError('Conflicting Linux dependency: ' + name)
            libraries[name] = source
    name: str
    source: Path
    for name, source in libraries.items():
        shutil.copy2(source, destination / name)
        ownership: subprocess.CompletedProcess[str] = subprocess.run(
            ['dpkg-query', '-S', str(source)], text=True, stdout=subprocess.PIPE,
            stderr=subprocess.PIPE, check=False, timeout=15)
        if ownership.returncode == 0:
            package: str = ownership.stdout.split(': ', 1)[0]
            package = package.split(':', 1)[0]
            notice: Path = Path('/usr/share/doc') / package / 'copyright'
            if notice.is_file():
                notices: Path = destination.parent / 'licenses/system'
                notices.mkdir(parents=True, exist_ok=True)
                shutil.copy2(notice, notices / (package + '.copyright'))

    for binary in list(destination.iterdir()) + executables:
        run(['patchelf', '--set-rpath', '$ORIGIN/../lib', str(binary)])


def main() -> None:
    parser: argparse.ArgumentParser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--sdk', type=Path, required=True)
    parser.add_argument('--platform', choices=['windows-x64', 'linux-x64'], required=True)
    parser.add_argument('--headless-only', action='store_true')
    parser.add_argument('--installer', action='store_true')
    arguments: argparse.Namespace = parser.parse_args()
    if arguments.installer and arguments.headless_only:
        parser.error('Installer verification requires native startup checks')
    build: Path = arguments.build.resolve()
    verify_build_version(build)
    sdk: Path = arguments.sdk.resolve()
    windows: bool = arguments.platform == 'windows-x64'
    revision: str = run(['git', 'rev-parse', 'HEAD']).strip()
    output: Path = build / 'dist'
    output.mkdir(exist_ok=True)
    stage: Path = Path(tempfile.mkdtemp(prefix='package-' + arguments.platform + '-', dir=build))
    root: Path = stage / 'SwiftEdit'
    root.mkdir()
    binaries: Path = root if windows else root / 'bin'
    binaries.mkdir(exist_ok=True)
    libraries: Path = root if windows else root / 'lib'
    libraries.mkdir(exist_ok=True)
    suffix: str = '.exe' if windows else ''
    originals: list[Path] = []
    packaged: list[Path] = []
    name: str
    for name in ['SwiftEdit', 'swiftedit-terminal', 'swiftedit-cli']:
        source: Path = build / (name + suffix)
        target: Path = binaries / source.name
        shutil.copy2(source, target)
        originals.append(source)
        packaged.append(target)
    if windows:
        windows_libraries(originals, libraries, sdk)
    else:
        linux_libraries(packaged, libraries)
        for name in ['SwiftEdit', 'swiftedit-terminal', 'swiftedit-cli']:
            launcher: Path = root / name
            launcher.write_text('#!/bin/sh\nroot=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)\n'
                                'export GUI_FORMS_FONT_DIR="$root/fonts"\n'
                                'export LD_LIBRARY_PATH="$root/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"\n'
                                'exec "$root/bin/' + name + '" "$@"\n', encoding='utf-8')
            launcher.chmod(0o755)
    shutil.copytree(sdk / 'gui-forms-sdk/share/GUIForms/fonts', root / 'fonts')
    shutil.copytree('third_party/md4c', root / 'licenses/MD4C', ignore=shutil.ignore_patterns('*.c', '*.h'))
    shutil.copy2('third_party/unicode/LICENSE.txt', root / 'licenses/Unicode.txt')
    component: str
    for component in ['gui-forms-sdk', 'picker-sdk']:
        notices: Path = sdk / component / 'share/licenses'
        if notices.is_dir():
            shutil.copytree(notices, root / 'licenses' / component)
    if windows:
        compiler: str | None = shutil.which('objdump')
        if compiler is None:
            raise RuntimeError('Compiler tools unavailable')
        license_root: Path = Path(compiler).parent.parent / 'share/licenses'
        for component in ['libc++', 'libunwind', 'compiler-rt', 'crt', 'headers',
                          'gcc', 'libgcc', 'winpthreads', 'libwinpthread']:
            notices = license_root / component
            if notices.is_dir():
                shutil.copytree(notices, root / 'licenses' / component)

    environment: dict[str, str] = dict(os.environ)
    environment.pop('GUI_FORMS_FONT_DIR', None)
    environment.pop('LD_LIBRARY_PATH', None)
    if windows:
        environment['PATH'] = str(Path(os.environ['WINDIR']) / 'System32')
    terminal: Path = root / ('swiftedit-terminal' + suffix)
    cli: Path = root / ('swiftedit-cli' + suffix)
    run([str(terminal), '--help'], environment)
    response: subprocess.CompletedProcess[str] = subprocess.run(
        [str(cli)], input='info\nquit\n', env=environment, text=True,
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=True, timeout=15)
    if 'ready\tSwiftEdit\t1' not in response.stdout or 'ok\tquit' not in response.stdout:
        raise RuntimeError('Packaged CLI smoke failed: ' + response.stdout)
    startup: str = 'not run (headless-only packaging verification)'
    if not arguments.headless_only:
        log: Path = stage / 'startup.log'
        with log.open('wb') as stream:
            child: subprocess.Popen[bytes] = subprocess.Popen(
                [str(root / ('SwiftEdit' + suffix))], env=environment, stdout=stream, stderr=stream)
            try:
                child.wait(timeout=3)
                raise RuntimeError('Packaged GUI exited during startup; inspect ' + str(log))
            except subprocess.TimeoutExpired:
                startup = 'owned packaged GUI remained running for three seconds'
            finally:
                if child.poll() is None:
                    child.terminate()
                    child.wait(timeout=10)
    manifest: dict[str, object] = {
        'release_version': project_version(),
        'product': 'SwiftEdit', 'platform': arguments.platform, 'source_revision': revision,
        'sdk': json.loads((sdk / 'manifest.json').read_text(encoding='utf-8')),
        'startup': startup, 'terminal_cli': 'packaged help and command-session smoke passed',
        'baseline': 'Windows x64 (tested Windows Server 2022)' if windows else 'Ubuntu 24.04 x64 / glibc 2.39 or newer',
        'files': {}}
    hashes: dict[str, str] = {}
    item: Path
    for item in sorted(root.rglob('*')):
        if item.is_file():
            hashes[item.relative_to(root).as_posix()] = digest(item)
    manifest['files'] = hashes
    (root / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    (root / 'README.txt').write_text(
        'SwiftEdit cross-platform dogfood build\n\nExtract the entire archive together. '
        'Run SwiftEdit' + suffix + ' for the GUI; swiftedit-terminal' + suffix +
        ' for terminal editing; swiftedit-cli' + suffix + ' for the command protocol.\n'
        'This build is unsigned. Linux requires an X11 session and Ubuntu 24.04-compatible runtime.\n'
        'The GUI currently has a 1 MiB UTF-8 / 4096-byte logical-line limit; large-file '
        'operations are available in the terminal and CLI. Expanded GUI features and idle CPU investigation remain open.\n',
        encoding='utf-8')
    archive: Path
    if windows:
        archive = output / ('SwiftEdit-' + arguments.platform + '-' + revision[:12] + '.zip')
        with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED) as bundle:
            for item in sorted(root.rglob('*')):
                if item.is_file():
                    bundle.write(item, item.relative_to(stage))
    else:
        archive = output / ('SwiftEdit-' + arguments.platform + '-' + revision[:12] + '.tar.gz')
        with tarfile.open(archive, 'w:gz') as bundle:
            bundle.add(root, arcname='SwiftEdit')
    if arguments.installer:
        manifest['installer'] = build_and_verify(root, output, arguments.platform)
    (output / (arguments.platform + '-manifest.json')).write_text(
        json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    archive.with_name(archive.name + '.sha256').write_text(digest(archive) + '  ' + archive.name + '\n', encoding='utf-8')
    print('Packaged ' + str(archive), flush=True)


if __name__ == '__main__':
    main()
