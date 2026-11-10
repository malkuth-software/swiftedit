"""Build public installed packages using the standalone GUI.Forms cache contract."""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import subprocess
import sys
import tarfile
import tomllib
from typing import BinaryIO

ROOT: Path = Path(__file__).resolve().parents[1]


def run(arguments: list[str]) -> None:
    print('+ ' + ' '.join(arguments), flush=True)
    subprocess.run(arguments, check=True)


def digest(path: Path) -> str:
    with path.open('rb') as stream:
        result: str = hashlib.file_digest(stream, 'sha256').hexdigest()
    return result


def source(path: Path, repository: str, revision: str) -> None:
    if not path.exists():
        run(['git', 'clone', '--filter=blob:none', '--no-checkout', 'https://github.com/' + repository + '.git', str(path)])
        run(['git', '-C', str(path), 'checkout', '--detach', revision])
    actual: str = subprocess.check_output(['git', '-C', str(path), 'rev-parse', 'HEAD'], text=True).strip()
    dirty: str = subprocess.check_output(['git', '-C', str(path), 'status', '--porcelain', '--untracked-files=no'], text=True)
    if dirty:
        raise RuntimeError('Dependency checkout is not the clean pinned source: ' + str(path))
    if actual != revision:
        run(['git', '-C', str(path), 'fetch', 'origin', revision])
        run(['git', '-C', str(path), 'checkout', '--detach', revision])


def import_cache(archive: Path, destination: Path, expected: str, repository: str, revision: str,
                 platform: str) -> None:
    # Validate all metadata and paths before writing any cache/runtime entries.
    if digest(archive) != expected:
        raise ValueError('Provider cache digest differs from the dependency lock')
    with tarfile.open(archive, 'r:gz') as bundle:
        manifest_file: BinaryIO | None = bundle.extractfile('cache-manifest.json')
        if manifest_file is None:
            raise ValueError('Missing provider cache manifest')
        with manifest_file:
            manifest: dict = json.load(manifest_file)
        if (manifest.get('revision') != revision or manifest.get('platform') != platform
                or manifest.get('repository') != repository):
            raise ValueError('Provider cache source/platform mismatch')
        members: list[tarfile.TarInfo] = []
        member: tarfile.TarInfo
        for member in bundle.getmembers():
            name: str = member.name
            if not (name == '.ccache' or name.startswith('.ccache/')
                    or name == '.build/toolchain' or name.startswith('.build/toolchain/')):
                continue
            target: Path = (destination / name).resolve()
            if (not target.is_relative_to(destination.resolve()) or '\\' in name
                    or '..' in PurePosixPath(name).parts):
                raise ValueError('Unsafe provider cache path')
            if member.islnk() or not (member.isfile() or member.isdir() or member.issym()):
                raise ValueError('Unsupported provider cache entry')
            if member.issym() and ('/' in member.linkname or '\\' in member.linkname
                                   or member.linkname.startswith('.')):
                raise ValueError('Unsafe runtime link')
            members.append(member)
        bundle.extractall(destination, members=members, filter='data')


def restore_cache(platform: str, lock: dict) -> None:
    download: Path = ROOT / '.build/cache-import' / lock['revision']
    download.mkdir(parents=True, exist_ok=True)
    archive: Path = download / ('gui-forms-cache-' + platform + '.tar.gz')
    if not archive.exists():
        result: subprocess.CompletedProcess = subprocess.run([
            'gh', 'release', 'download', 'build-' + lock['revision'], '--repo', lock['repository'],
            '--pattern', archive.name, '--dir', str(download)], check=False)
        if result.returncode:
            print('Provider cache unavailable; compiling pinned source without imported objects.', flush=True)
            return
    import_cache(archive, ROOT, lock['cache_sha256'][platform], lock['repository'], lock['revision'], platform)
    print('Verified provider compiler cache imported for ' + platform, flush=True)


def prepare_runtime(gui: Path, jobs: str) -> None:
    runtime: Path = ROOT / '.build/toolchain/llvm-22.1.8-macos14'
    os.environ['GUI_FORMS_LLVM_RUNTIME'] = str(runtime)
    validator: list[str] = [sys.executable, str(gui / 'tools/macos_runtime_manifest.py'), str(runtime)]
    check: subprocess.CompletedProcess = subprocess.run(validator, check=False)
    if check.returncode:
        # Only the owned generated prefix may be replaced after failed validation.
        if not runtime.resolve().is_relative_to(ROOT.resolve()):
            raise RuntimeError('Runtime prefix resolves outside the workspace')
        shutil.rmtree(runtime, ignore_errors=True)
        run([sys.executable, str(gui / 'tools/build_macos_runtimes.py'), '--work',
             str(ROOT / '.build/llvm-runtimes'), '--prefix', str(runtime), '--jobs', jobs])
        run(validator)
    run([sys.executable, str(gui / 'tools/audit_macos_minimum.py'), str(runtime / 'lib')])


# Local builds only: SWIFTEDIT_LOCKSTEP names the machine's lockstep record
# (D:/malkuth/cache/lockstep/current-<target>.json), the one GUI.Forms build
# every product on this machine shares. Its installed prefix carries GUI.Forms
# and the stx packages, so nothing is built here: the record is checked and
# written to sdk-lockstep/manifest.json for Build-Native.py (app-lockstep).
LOCKSTEP_VARIABLE: str = 'SWIFTEDIT_LOCKSTEP'
LOCKSTEP_MODE: str = 'lockstep-installed-prefix'


def read_lockstep(platform: str) -> dict | None:
    """The lockstep record SWIFTEDIT_LOCKSTEP names, or None when it is unset."""
    named: str = os.environ.get(LOCKSTEP_VARIABLE, '')
    if not named:
        return None
    record: dict = json.loads(Path(named).read_text(encoding='utf-8'))
    if record.get('schema') != 1 or record.get('target') != platform:
        raise ValueError('Lockstep record ' + named + ' is not a schema 1 record for ' + platform)
    return record


def prepare_lockstep(record: dict, platform: str) -> None:
    """Records the lockstep prefix in sdk-lockstep/manifest.json; builds nothing."""
    build: Path = ROOT / ('.build/native-' + platform)
    sdk: Path = build / 'sdk-lockstep'
    sdk.mkdir(parents=True, exist_ok=True)
    provider: str = 'lockstep:' + record['commit']
    manifest: dict = {'provider_revision': provider,
        'stx_rtf2md_revision': provider, 'gui_forms_prefix': record['prefix'],
        'toolchain_file': record['toolchain_file'], 'font_dir': record['font_dir'],
        'llvm_runtime': record['llvm_runtime'], 'ccache_dir': record['ccache_dir'],
        'platform': platform, 'dependency_mode': LOCKSTEP_MODE,
        'compiler': subprocess.check_output(['clang', '--version'], text=True).strip()}
    (sdk / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')


def main() -> None:
    parser: argparse.ArgumentParser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--platform', choices=['windows-x64', 'linux-x64', 'linux-arm64', 'macos-arm64'], required=True)
    parser.add_argument('--jobs', type=int, default=2)
    parser.add_argument('--restore-cache', action='store_true')
    parser.add_argument('--fetch-only', action='store_true')
    arguments: argparse.Namespace = parser.parse_args()
    if arguments.jobs < 1:
        parser.error('--jobs must be positive')
    os.chdir(ROOT)
    lock: dict = json.loads((ROOT / 'ci/dependencies.json').read_text())
    lockstep: dict | None = read_lockstep(arguments.platform)
    if lockstep is not None:
        if arguments.restore_cache or arguments.fetch_only:
            parser.error('a lockstep build fetches and restores nothing')
        prepare_lockstep(lockstep, arguments.platform)
        return
    gui: Path = ROOT / 'gui_forms'
    source(gui, lock['repository'], lock['revision'])
    # GUI.Forms' build tools forward to template_harness, which they look for
    # beside the checkout (gui_forms/tools/harness_link.py): here, at the root,
    # at SwiftEdit's harness.lock, which must equal the GUI.Forms pin's.
    harness_lock: dict = tomllib.loads((ROOT / 'harness.lock').read_text(encoding='utf-8'))
    provider_harness: dict = tomllib.loads((gui / 'harness.lock').read_text(encoding='utf-8'))
    if harness_lock['harness'] != provider_harness['harness']:
        raise ValueError('harness.lock differs from gui_forms/harness.lock at the GUI.Forms pin')
    source(ROOT / 'template_harness', harness_lock['harness']['repository'], harness_lock['harness']['commit'])
    if arguments.restore_cache:
        restore_cache(arguments.platform, lock)
    if arguments.fetch_only:
        return
    jobs: str = str(arguments.jobs)
    if arguments.platform == 'macos-arm64':
        prepare_runtime(gui, jobs)
    build: Path = ROOT / ('.build/native-' + arguments.platform)
    sdk: Path = build / 'sdk'
    gui_sdk: Path = sdk / 'gui-forms-sdk'
    os.environ['CCACHE_DIR'] = str(ROOT / '.ccache')
    os.environ['CCACHE_BASEDIR'] = str(ROOT)
    os.environ['CCACHE_COMPILERCHECK'] = 'content'
    os.environ['CMAKE_C_COMPILER_LAUNCHER'] = 'ccache'
    os.environ['CMAKE_CXX_COMPILER_LAUNCHER'] = 'ccache'
    os.environ['CMAKE_OBJCXX_COMPILER_LAUNCHER'] = 'ccache'
    # GUI.Forms' own SDK build: the CI configuration with every optional
    # component, the renderer and the text sources from stx at its stx.lock
    # commit, and the stx packages (stx_rtf2md among them) installed into the
    # same prefix.
    run([sys.executable, '-B', str(gui / 'tools/build_sdk.py'), '--target', arguments.platform,
         '--prefix', str(gui_sdk), '--features', 'all', '--jobs', jobs, '--work', str(build / 'gui-forms')])
    manifest: dict = {'provider_revision': lock['revision'],
        'stx_rtf2md_revision': lock['revision'],
        'platform': arguments.platform, 'dependency_mode': 'pinned-source-public-installed-targets',
        'compiler': subprocess.check_output(['clang', '--version'], text=True).strip()}
    (sdk / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')


if __name__ == '__main__':
    main()
