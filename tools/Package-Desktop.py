"""Package tested Windows/Linux binaries: the programs alone, importing only system libraries."""
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


def windows_refusals(imports: list[str], system: Path) -> list[str]:
    """Each imported DLL that is not a Windows system DLL in `system`.

    `imports` are the DLL Name entries objdump -p lists. API sets resolve in
    the loader; every other DLL must lie in System32.
    """
    refused: list[str] = []
    dependency: str
    for dependency in imports:
        if dependency.casefold().startswith(('api-ms-win-', 'ext-ms-win-')):
            continue
        if not (system / dependency).is_file():
            refused.append(dependency)
    return refused


def windows_programs_only(executables: list[Path]) -> None:
    """Refuse a program importing any DLL but Windows' own (one module, 2026-11-15).

    GUI.Forms, the codecs and the LLVM runtimes are linked into each program,
    so nothing is copied beside it; a non-system import fails the package.
    """
    tool: str | None = shutil.which('objdump')
    if tool is None:
        raise RuntimeError('objdump is required for dependency inspection')
    system: Path = Path(os.environ['WINDIR']) / 'System32'
    failures: list[str] = []
    binary: Path
    for binary in executables:
        listing: str = run([tool, '-p', str(binary)])
        imports: list[str] = re.findall(r'DLL Name:\s*(\S+)', listing)
        dependency: str
        for dependency in windows_refusals(imports, system):
            failures.append(binary.name + ': ' + dependency)
    if failures:
        raise RuntimeError('A shipped program imports only system DLLs; found:\n  ' + '\n  '.join(failures))


# One module (owner, 2026-11-15): a Linux package carries the programs alone
# and uses the desktop's own glibc, X11 (or XWayland), ATK and GLib; it is
# built against no distribution's copies. A NEEDED entry must be one of
# these system libraries (the C library, its loader, libgcc_s, libutil for
# the terminal's pseudo-terminal, and the libraries GUI.Forms' Linux host
# links), and every library ldd resolves must lie in a system directory.
LINUX_SYSTEM_LIBRARY: re.Pattern[str] = re.compile(
    r'(?:lib(?:c|m|dl|pthread|rt|resolv|util|gcc_s)\.so\.\d+'
    r'|ld-linux[\w.-]*\.so\.\d+|linux-(?:vdso|gate)\.so\.1'
    r'|libX(?:11|fixes|render|cursor|randr|ext|i)\.so\.\d+'
    r'|libatk-1\.0\.so\.0|libatk-bridge-2\.0\.so\.0'
    r'|lib(?:gio|gobject|glib)-2\.0\.so\.0)')
LINUX_SYSTEM_DIRECTORIES: tuple[str, ...] = ('/lib/', '/lib32/', '/lib64/', '/usr/lib/', '/usr/lib32/', '/usr/lib64/')


def linux_refusals(needed_listing: str, ldd_listing: str) -> list[str]:
    """Each NEEDED entry or ldd line that is not the desktop's own library.

    `needed_listing` is `readelf -d` output and `ldd_listing` is `ldd` output
    for the same binary. An empty list means it imports only system
    libraries.
    """
    refused: list[str] = []
    name: str
    for name in re.findall(r'\(NEEDED\)\s+Shared library: \[([^\]]+)\]', needed_listing):
        if LINUX_SYSTEM_LIBRARY.fullmatch(name) is None:
            refused.append(name)
    line: str
    for line in ldd_listing.splitlines():
        line = line.strip()
        if not line or 'statically linked' in line:
            continue
        if 'not found' in line:
            refused.append(line)
            continue
        match: re.Match[str] | None = re.search(r'=>\s*(/\S+)', line)
        if match is None:
            match = re.match(r'(/\S+)', line)
        if match is not None and not match.group(1).startswith(LINUX_SYSTEM_DIRECTORIES):
            refused.append(line)
    return refused


def linux_programs_only(executables: list[Path]) -> None:
    """Refuse a program importing a non-system library; clear build RUNPATHs."""
    failures: list[str] = []
    binary: Path
    for binary in executables:
        refused: list[str] = linux_refusals(run(['readelf', '-d', str(binary)]), run(['ldd', str(binary)]))
        name: str
        for name in refused:
            failures.append(binary.name + ': ' + name)
    if failures:
        raise RuntimeError('A shipped program imports only system libraries; found:\n  ' + '\n  '.join(failures))
    for binary in executables:
        run(['patchelf', '--remove-rpath', str(binary)])


def main() -> None:
    parser: argparse.ArgumentParser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--sdk', type=Path, required=True)
    parser.add_argument('--platform', choices=['windows-x64', 'linux-x64', 'linux-arm64'], required=True)
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
        windows_programs_only(originals)
    else:
        linux_programs_only(packaged)
        for name in ['SwiftEdit', 'swiftedit-terminal', 'swiftedit-cli']:
            launcher: Path = root / name
            launcher.write_text('#!/bin/sh\nroot=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)\n'
                                'exec "$root/bin/' + name + '" "$@"\n', encoding='utf-8')
            launcher.chmod(0o755)
    # The SDK's font directory holds the bundled faces' licences only; the
    # faces are compiled into GUI.Forms (font pack stage 4).
    shutil.copytree(sdk / 'gui-forms-sdk/share/GUIForms/fonts', root / 'licenses/fonts')
    shutil.copytree('third_party/md4c', root / 'licenses/MD4C', ignore=shutil.ignore_patterns('*.c', '*.h'))
    shutil.copy2('third_party/unicode/LICENSE.txt', root / 'licenses/Unicode.txt')
    component: str
    for component in ['gui-forms-sdk']:
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
        'This build is unsigned. Linux requires an X11 session (or XWayland); the programs use the desktop\'s own X11 and C library.\n'
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
