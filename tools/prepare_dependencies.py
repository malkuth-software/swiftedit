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
# and the stx packages, so only the picker is built here, in trees apart from
# the pinned ones (picker-lockstep, sdk-lockstep, app-lockstep).
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


def build_picker(picker: Path, tree: Path, sdk: Path, common: list[str], gui_prefix: Path, provider: str,
                 platform: str, jobs: str) -> None:
    """Builds the document picker against the GUI.Forms installed at gui_prefix into sdk/picker-sdk."""
    consumption: Path = tree.parent / (tree.name + '-consumption.json')
    consumption.parent.mkdir(parents=True, exist_ok=True)
    consumption.write_text(json.dumps({'identity': {
        'id': 'swiftedit-' + platform + '-' + provider,
        'state': 'development', 'source_revision': provider},
        'validation': 'Consumer tests must pass before packaging.'}, indent=2) + '\n')
    run(['cmake', '-S', str(picker / 'frontend'), '-B', str(tree)] + common + [
        '-DCMAKE_PREFIX_PATH=' + str(gui_prefix), '-DFILE_MANAGER_BUILD_TESTS=OFF',
        '-DFILE_MANAGER_GUI_FORMS_MANIFEST=' + str(consumption)])
    run(['cmake', '--build', str(tree), '--target', 'file_manager_document_picker_view', '--parallel', jobs])
    run(['cmake', '--install', str(tree), '--prefix', str(sdk / 'picker-sdk'), '--component', 'DocumentPicker'])
    notices: Path = sdk / 'picker-sdk/share/licenses/FileManagerDocumentPicker'
    notices.mkdir(parents=True, exist_ok=True)
    shutil.copy2(picker / 'LICENSE', notices / 'LICENSE')


def prepare_lockstep(record: dict, lock: dict, platform: str, jobs: str) -> None:
    """Builds only the pinned picker against the lockstep prefix and records it in sdk-lockstep/manifest.json."""
    picker: Path = ROOT / '.build/dependencies/file_manager'
    source(picker, lock['picker_repository'], lock['picker_revision'])
    run(['git', '-C', str(picker), 'submodule', 'update', '--init', '--depth', '1', '--', 'backend'])
    build: Path = ROOT / ('.build/native-' + platform)
    sdk: Path = build / 'sdk-lockstep'
    # The shared compiler cache, with SwiftEdit's own checkout as the base.
    os.environ['CCACHE_DIR'] = record['ccache_dir']
    os.environ['CCACHE_BASEDIR'] = str(ROOT)
    os.environ['CCACHE_COMPILERCHECK'] = 'content'
    if record['llvm_runtime']:
        os.environ['GUI_FORMS_LLVM_RUNTIME'] = record['llvm_runtime']
    common: list[str] = ['-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release',
        '-DCMAKE_TOOLCHAIN_FILE=' + record['toolchain_file'],
        '-DCMAKE_C_COMPILER_LAUNCHER=ccache', '-DCMAKE_CXX_COMPILER_LAUNCHER=ccache',
        '-DCMAKE_OBJCXX_COMPILER_LAUNCHER=ccache']
    provider: str = 'lockstep:' + record['commit']
    build_picker(picker, build / 'picker-lockstep', sdk, common, Path(record['prefix']), provider, platform, jobs)
    manifest: dict = {'provider_revision': provider, 'picker_revision': lock['picker_revision'],
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
        prepare_lockstep(lockstep, lock, arguments.platform, str(arguments.jobs))
        return
    gui: Path = ROOT / 'gui_forms'
    picker: Path = ROOT / '.build/dependencies/file_manager'
    source(gui, lock['repository'], lock['revision'])
    # GUI.Forms' build tools forward to template_harness, which they look for
    # beside the checkout (gui_forms/tools/harness_link.py): here, at the root,
    # at SwiftEdit's harness.lock, which must equal the GUI.Forms pin's.
    harness_lock: dict = tomllib.loads((ROOT / 'harness.lock').read_text(encoding='utf-8'))
    provider_harness: dict = tomllib.loads((gui / 'harness.lock').read_text(encoding='utf-8'))
    if harness_lock['harness'] != provider_harness['harness']:
        raise ValueError('harness.lock differs from gui_forms/harness.lock at the GUI.Forms pin')
    source(ROOT / 'template_harness', harness_lock['harness']['repository'], harness_lock['harness']['commit'])
    source(picker, lock['picker_repository'], lock['picker_revision'])
    # The standalone stx_rtf2md package builds from the same GUI.Forms checkout.
    # SWIFTEDIT_STX_RTF2MD_SOURCE may name a local tree containing stx_rtf2md/
    # for development before a revision lands; the manifest then records it,
    # and Build-Native.py refuses to package it.
    stx_override: str = os.environ.get('SWIFTEDIT_STX_RTF2MD_SOURCE', '')
    stx: Path = Path(stx_override) if stx_override else gui
    # Frontend CMake declares the public backend client even for picker-only builds.
    # Initialize its recorded gitlink, but do not build or package backend services.
    run(['git', '-C', str(picker), 'submodule', 'update', '--init', '--depth', '1', '--', 'backend'])
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
    os.environ['BUILD_JOBS'] = jobs
    # The provider's renderer scripts inspect the environment before CMake runs.
    os.environ['CMAKE_C_COMPILER_LAUNCHER'] = 'ccache'
    os.environ['CMAKE_CXX_COMPILER_LAUNCHER'] = 'ccache'
    os.environ['CMAKE_OBJCXX_COMPILER_LAUNCHER'] = 'ccache'
    common: list[str] = ['-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release',
        '-DCMAKE_TOOLCHAIN_FILE=' + str(gui / 'cmake/llvm22.cmake'),
        '-DCMAKE_C_COMPILER_LAUNCHER=ccache', '-DCMAKE_CXX_COMPILER_LAUNCHER=ccache',
        '-DCMAKE_OBJCXX_COMPILER_LAUNCHER=ccache']
    options: list[str] = ['-DCMAKE_INSTALL_LIBDIR=lib', '-DCMAKE_POSITION_INDEPENDENT_CODE=ON',
        '-DGUI_FORMS_BUILD_GALLERY=OFF', '-DGUI_FORMS_BUILD_TESTS=OFF', '-DGUI_FORMS_BUILD_AUDIO=ON',
        '-DCMAKE_INSTALL_PREFIX=' + str(gui_sdk)]
    if arguments.platform == 'windows-x64':
        # The provider's Win32 host draws text with its HarfBuzz/FreeType engine (ADR-004).
        run(['sh', str(gui / 'third_party/fetch_text_stack.sh')])
        options.extend(['-DGUI_FORMS_ENABLE_SKIA=OFF', '-DGUI_FORMS_ENABLE_HARFBUZZ_TEXT=ON'])
    else:
        linux: bool = arguments.platform.startswith('linux-')
        if linux:
            os.environ['GUI_FORMS_SYSTEM_BUILD_TOOLS'] = '1'
        run(['sh', str(gui / 'third_party/fetch_skia_cpu.sh')])
        run(['sh', str(gui / 'third_party/fetch_text_stack.sh')])
        skia: Path = build / 'skia'
        # The Linux script builds for the host's architecture (x64 or arm64).
        script: str = 'build_skia_cpu_linux.sh' if linux else 'build_skia_cpu.sh'
        run(['sh', str(gui / 'third_party' / script), str(skia)])
        options.extend(['-DGUI_FORMS_SKIA_PREBUILT=ON', '-DGUI_FORMS_SKIA_OUT=' + str(skia)])
        if linux:
            options.append('-DGUI_FORMS_ENABLE_LINUX_HOST=ON')
    # The text engine's pinned UAX #14 line breaking (libunibreak), required
    # wherever HarfBuzz text is on, which is now every platform.
    run(['sh', str(gui / 'third_party/fetch_linebreak.sh')])
    run(['cmake', '-S', str(gui), '-B', str(build / 'gui-forms')] + common + options)
    run(['cmake', '--build', str(build / 'gui-forms'), '--parallel', jobs])
    run(['cmake', '--install', str(build / 'gui-forms')])
    build_picker(picker, build / 'picker', sdk, common, gui_sdk, lock['revision'], arguments.platform, jobs)
    run(['cmake', '-S', str(stx / 'stx_rtf2md'), '-B', str(build / 'stx-rtf2md')] + common + [
        '-DCMAKE_INSTALL_LIBDIR=lib', '-DCMAKE_POSITION_INDEPENDENT_CODE=ON', '-DSTX_RTF2MD_TESTS=OFF',
        '-DCMAKE_INSTALL_PREFIX=' + str(sdk / 'stx-rtf2md-sdk')])
    run(['cmake', '--build', str(build / 'stx-rtf2md'), '--parallel', jobs])
    run(['cmake', '--install', str(build / 'stx-rtf2md')])
    stx_revision: str = 'local:' + str(stx) if stx_override else lock['revision']
    manifest: dict = {'provider_revision': lock['revision'], 'picker_revision': lock['picker_revision'],
        'stx_rtf2md_revision': stx_revision,
        'platform': arguments.platform, 'dependency_mode': 'pinned-source-public-installed-targets',
        'compiler': subprocess.check_output(['clang', '--version'], text=True).strip()}
    (sdk / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')


if __name__ == '__main__':
    main()
