"""Stage, resolve, sign and archive the tested macOS SwiftEdit application."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import tempfile
import time
from typing import BinaryIO, Protocol
from publication_version import project_version, verify_build_version
from installers import build_and_verify


class Digest(Protocol):
    def update(self, data: bytes) -> None: ...
    def hexdigest(self) -> str: ...


def run(arguments: list[str]) -> str:
    result: subprocess.CompletedProcess[str] = subprocess.run(
        arguments, check=False, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    print(result.stdout, end='')
    result.check_returncode()
    return result.stdout


def sha256(path: Path) -> str:
    digest: Digest = hashlib.sha256()
    stream: BinaryIO
    with path.open('rb') as stream:
        data: bytes = stream.read(1024 * 1024)
        while data:
            digest.update(data)
            data = stream.read(1024 * 1024)
    result: str = digest.hexdigest()
    return result


def verify_load_paths(binary: Path, executable: Path, app: Path) -> None:
    listing: str = run(['otool', '-L', str(binary)])
    identity_listing: str = run(['otool', '-D', str(binary)])
    identities: list[str] = identity_listing.splitlines()[1:]
    line: str
    for line in listing.splitlines()[1:]:
        dependency: str = line.strip().split(' (', 1)[0]
        if dependency in identities or dependency.startswith(('/System/', '/usr/lib/')):
            continue
        expanded: str = dependency.replace('@loader_path', str(binary.parent))
        expanded = expanded.replace('@executable_path', str(executable.parent))
        resolved: Path = Path(expanded).resolve()
        if '@' in expanded or not resolved.is_relative_to(app) or not resolved.is_file():
            raise RuntimeError('Unresolved external runtime dependency: ' + dependency)


def cpu_seconds(value: str) -> float:
    fields: list[str] = value.strip().split(':')
    if len(fields) < 2 or len(fields) > 3:
        raise RuntimeError('Unexpected process CPU time: ' + value)
    result: float = 0.0
    field: str
    for field in fields:
        result = result * 60.0 + float(field)
    return result


def read_cpu_seconds(pid: int) -> float:
    sample: str = run(['/bin/ps', '-p', str(pid), '-o', 'time='])
    result: float = cpu_seconds(sample)
    return result


def wait_alive(child: subprocess.Popen[bytes], seconds: int, log: Path) -> None:
    try:
        child.wait(timeout=seconds)
    except subprocess.TimeoutExpired:
        return
    raise RuntimeError('Packaged app exited during observation; inspect ' + str(log))


def sample_idle_stack(pid: int, stage: Path) -> str:
    """Sample only the owned child, after the CPU interval to avoid measurement overlap."""
    report: Path = stage / 'idle-stack.txt'
    diagnostic: Path = stage / 'idle-stack-status.txt'
    try:
        result: subprocess.CompletedProcess[str] = subprocess.run(
            ['/usr/bin/sample', str(pid), '5', '1', '-file', str(report)],
            check=False, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            timeout=30)
    except subprocess.TimeoutExpired:
        diagnostic.write_text('Stack sampling exceeded the 30-second bound.\n', encoding='utf-8')
        return 'unavailable: sampling timeout'
    diagnostic.write_text(result.stdout, encoding='utf-8')
    if result.returncode != 0 or not report.is_file():
        return 'unavailable: inspect idle-stack-status.txt'
    return 'captured: idle-stack.txt; five seconds at one millisecond intervals'


def verify_startup(executable: Path, stage: Path) -> dict[str, object]:
    """Own one child and a disposable fixture; always reap the child on exit."""
    fixture: Path = stage / 'startup-fixture.txt'
    fixture.write_text('', encoding='utf-8')
    log: Path = stage / 'startup.log'
    environment: dict[str, str] = dict(os.environ)
    variable: str
    for variable in ['GUI_FORMS_FONT_DIR', 'DYLD_LIBRARY_PATH', 'DYLD_FALLBACK_LIBRARY_PATH']:
        environment.pop(variable, None)
    stream: BinaryIO
    with log.open('wb') as stream:
        child: subprocess.Popen[bytes] = subprocess.Popen(
            [str(executable), str(fixture)], cwd=stage, env=environment,
            stdout=stream, stderr=subprocess.STDOUT)
        try:
            wait_alive(child, 5, log)
            before: float = read_cpu_seconds(child.pid)
            started: float = time.monotonic()
            wait_alive(child, 10, log)
            after: float = read_cpu_seconds(child.pid)
            elapsed: float = time.monotonic() - started
            consumed: float = after - before
            if consumed < 0:
                raise RuntimeError('Process CPU time moved backwards')
            observation: dict[str, object] = {
                'warmup_seconds': 5, 'elapsed_seconds': elapsed,
                'process_cpu_seconds': consumed,
                'percent_of_one_core': 100.0 * consumed / elapsed,
                'scope': 'ten quiet seconds with an empty text fixture on the CI runner; '
                         'focus and occlusion not controlled; no idle-performance pass threshold'}
            print('Packaged idle CPU: ' + json.dumps(observation))
            observation['stack_sample'] = sample_idle_stack(child.pid, stage)
            wait_alive(child, 1, log)
            return observation
        finally:
            if child.poll() is None:
                child.terminate()
                try:
                    child.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    child.kill()
                    child.wait()


def main() -> None:
    parser: argparse.ArgumentParser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--sdk', type=Path, required=True)
    parser.add_argument('--installer', action='store_true')
    arguments: argparse.Namespace = parser.parse_args()
    if platform.system() != 'Darwin':
        raise RuntimeError('This packager requires a native macOS runner')
    build: Path = arguments.build.resolve(strict=True)
    verify_build_version(build)
    sdk: Path = arguments.sdk.resolve(strict=True)
    revision: str = run(['git', 'rev-parse', 'HEAD']).strip()
    status: str = run(['git', 'status', '--porcelain'])
    if status:
        raise RuntimeError('Dogfood archives require a clean checkout')
    output: Path = build / 'dist'
    output.mkdir(exist_ok=True)
    stage: Path = Path(tempfile.mkdtemp(prefix='mac-package-', dir=build))
    app: Path = stage / 'SwiftEdit.app'
    shutil.copytree(build / 'SwiftEdit.app', app, symlinks=True)
    terminal: Path = app / 'Contents/MacOS/SwiftEdit-terminal'
    shutil.copy2(build / 'swiftedit-terminal', terminal)
    cli: Path = app / 'Contents/MacOS/swiftedit-cli'
    shutil.copy2(build / 'swiftedit-cli', cli)
    resources: Path = app / 'Contents/Resources'
    shutil.copy2('docs/COMMAND_PROTOCOL.md', resources / 'COMMAND_PROTOCOL.md')
    notices: Path = resources / 'licenses'
    runtime: Path = Path(os.environ['GUI_FORMS_LLVM_RUNTIME']).resolve()
    shutil.copytree(sdk / 'gui-forms-sdk/share/licenses', notices, dirs_exist_ok=True)
    shutil.copytree(sdk / 'picker-sdk/share/licenses', notices, dirs_exist_ok=True)
    shutil.copytree(runtime / 'share/licenses', notices, dirs_exist_ok=True)
    shutil.copy2('third_party/md4c/LICENSE.md', notices / 'MD4C.md')
    shutil.copy2('third_party/unicode/LICENSE.txt', notices / 'Unicode.txt')
    fixup: Path = stage / 'fixup.cmake'
    fixup.write_text('include(BundleUtilities)\nfixup_bundle("' + app.as_posix() +
                     '" "' + terminal.as_posix() + ';' + cli.as_posix() + '" "' +
                     (sdk / 'gui-forms-sdk/lib').as_posix() + ';' +
                     (runtime / 'lib').as_posix() + '")\n', encoding='utf-8')
    run(['cmake', '-P', str(fixup)])
    executable: Path = app / 'Contents/MacOS/SwiftEdit'
    verify_load_paths(executable, executable, app)
    verify_load_paths(terminal, terminal, app)
    verify_load_paths(cli, cli, app)
    frameworks: Path = app / 'Contents/Frameworks'
    library: Path
    for library in frameworks.iterdir():
        if library.is_file() and not library.is_symlink():
            verify_load_paths(library, executable, app)
            run(['codesign', '--force', '--sign', '-', str(library)])
    run(['codesign', '--force', '--sign', '-', str(terminal)])
    run(['codesign', '--force', '--sign', '-', str(cli)])
    run(['codesign', '--force', '--deep', '--sign', '-', str(app)])
    run(['codesign', '--verify', '--deep', '--strict', str(app)])
    idle_cpu: dict[str, object] = verify_startup(executable, stage)
    terminal_environment: dict[str, str] = dict(os.environ)
    variable: str
    for variable in ['GUI_FORMS_FONT_DIR', 'DYLD_LIBRARY_PATH', 'DYLD_FALLBACK_LIBRARY_PATH']:
        terminal_environment.pop(variable, None)
    subprocess.run([sys.executable, '-B', 'tests/terminal_posix_smoke.py',
                    '--executable', str(terminal)], env=terminal_environment, check=True, timeout=45)
    response: subprocess.CompletedProcess[str] = subprocess.run(
        [str(cli)], input='info\nquit\n', cwd=stage, env=terminal_environment, text=True,
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=True, timeout=15)
    if 'ready\tSwiftEdit\t1' not in response.stdout or 'ok\tquit' not in response.stdout:
        raise RuntimeError('Packaged CLI smoke failed: ' + response.stdout)
    manifest: dict[str, object] = {
        'release_version': project_version(),
        'product': 'SwiftEdit', 'source_revision': revision,
        'platform': platform.platform(), 'architecture': platform.machine(),
        'signature': 'ad-hoc; not Developer ID signed or notarized',
        'packaged_startup': 'remained running for five seconds on a disposable text fixture',
        'idle_cpu_observation': idle_cpu,
        'sdk': json.loads((sdk / 'manifest.json').read_text(encoding='utf-8')),
        'executable_sha256': sha256(executable),
        'terminal_sha256': sha256(terminal),
        'cli_sha256': sha256(cli),
        'cli_validation': 'packaged command session passed info/quit smoke without SDK library overrides',
        'terminal_validation': 'packaged executable passed owned PTY paste/save and mode restoration smoke'}
    (output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    (output / 'README.txt').write_text(
        'SwiftEdit development build for Apple silicon\n\n'
        'Extract the ZIP, then open SwiftEdit.app. The app includes its required private '
        'libraries, fonts and license notices. This build is ad-hoc signed, not notarized.\n'
        'Built and tested on Apple-silicon macOS 15 with the GUI.Forms macOS 14 runtime contract. Intel Macs are not supported.\n\n'
        'Interactive terminal: run ./SwiftEdit.app/Contents/MacOS/SwiftEdit-terminal [file] '
        'from Terminal. Use --help for shortcuts. The terminal clipboard is private; '
        'ordinary terminal-emulator paste is supported through bracketed paste.\n\n'
        'Command session: run ./SwiftEdit.app/Contents/MacOS/swiftedit-cli from Terminal '
        'or a client process. Protocol documentation is in '
        'SwiftEdit.app/Contents/Resources/COMMAND_PROTOCOL.md.\n\n'
        'This is an early dogfood build. The expanded feature set is still in development. '
        'The GUI currently uses its older editor path, limited to 1 MiB of UTF-8 text '
        'and 4096 UTF-8 bytes per logical line; '
        'the newer large-file session and terminal work are not yet integrated into the GUI. '
        'Use copies of important documents while testing.\n'
        'See manifest.json for the exact source and dependency revisions.\n', encoding='utf-8')
    archive: Path = output / ('SwiftEdit-macos-arm64-' + revision[:12] + '.zip')
    run(['ditto', '-c', '-k', '--sequesterRsrc', '--keepParent', str(app), str(archive)])
    checksum: str = sha256(archive)
    archive.with_suffix('.zip.sha256').write_text(checksum + '  ' + archive.name + '\n', encoding='utf-8')
    if arguments.installer:
        manifest['installer'] = build_and_verify(app, output, 'macos-arm64')
        (output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')


if __name__ == '__main__':
    main()
