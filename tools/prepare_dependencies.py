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


def import_cache(archive: Path, destination: Path, expected: str, revision: str, platform: str) -> None:
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
                or manifest.get('repository') != 'falseywinchnet/gui_forms'):
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
    import_cache(archive, ROOT, lock['cache_sha256'][platform], lock['revision'], platform)
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


def main() -> None:
    parser: argparse.ArgumentParser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--platform', choices=['windows-x64', 'linux-x64', 'macos-arm64'], required=True)
    parser.add_argument('--jobs', type=int, default=2)
    parser.add_argument('--restore-cache', action='store_true')
    parser.add_argument('--fetch-only', action='store_true')
    arguments: argparse.Namespace = parser.parse_args()
    if arguments.jobs < 1:
        parser.error('--jobs must be positive')
    os.chdir(ROOT)
    lock: dict = json.loads((ROOT / 'ci/dependencies.json').read_text())
    gui: Path = ROOT / 'gui_forms'
    picker: Path = ROOT / '.build/dependencies/file_manager'
    source(gui, lock['repository'], lock['revision'])
    source(picker, lock['picker_repository'], lock['picker_revision'])
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
        options.extend(['-DGUI_FORMS_ENABLE_SKIA=OFF', '-DGUI_FORMS_ENABLE_HARFBUZZ_TEXT=OFF'])
    else:
        if arguments.platform == 'linux-x64':
            os.environ['GUI_FORMS_SYSTEM_BUILD_TOOLS'] = '1'
        run(['sh', str(gui / 'third_party/fetch_skia_cpu.sh')])
        run(['sh', str(gui / 'third_party/fetch_text_stack.sh')])
        skia: Path = build / 'skia'
        script: str = 'build_skia_cpu_linux.sh' if arguments.platform == 'linux-x64' else 'build_skia_cpu.sh'
        run(['sh', str(gui / 'third_party' / script), str(skia)])
        options.extend(['-DGUI_FORMS_SKIA_PREBUILT=ON', '-DGUI_FORMS_SKIA_OUT=' + str(skia)])
        if arguments.platform == 'linux-x64':
            options.append('-DGUI_FORMS_ENABLE_LINUX_HOST=ON')
    run(['cmake', '-S', str(gui), '-B', str(build / 'gui-forms')] + common + options)
    run(['cmake', '--build', str(build / 'gui-forms'), '--parallel', jobs])
    run(['cmake', '--install', str(build / 'gui-forms')])
    consumption: Path = build / 'picker-consumption.json'
    consumption.write_text(json.dumps({'identity': {
        'id': 'swiftedit-' + arguments.platform + '-' + lock['revision'],
        'state': 'development', 'source_revision': lock['revision']},
        'validation': 'Consumer tests must pass before packaging.'}, indent=2) + '\n')
    run(['cmake', '-S', str(picker / 'frontend'), '-B', str(build / 'picker')] + common + [
        '-DCMAKE_PREFIX_PATH=' + str(gui_sdk), '-DFILE_MANAGER_BUILD_TESTS=OFF',
        '-DFILE_MANAGER_GUI_FORMS_MANIFEST=' + str(consumption)])
    run(['cmake', '--build', str(build / 'picker'), '--target', 'file_manager_document_picker_view', '--parallel', jobs])
    run(['cmake', '--install', str(build / 'picker'), '--prefix', str(sdk / 'picker-sdk'), '--component', 'DocumentPicker'])
    notices: Path = sdk / 'picker-sdk/share/licenses/FileManagerDocumentPicker'
    notices.mkdir(parents=True, exist_ok=True)
    shutil.copy2(picker / 'LICENSE', notices / 'LICENSE')
    manifest: dict = {'provider_revision': lock['revision'], 'picker_revision': lock['picker_revision'],
        'platform': arguments.platform, 'dependency_mode': 'pinned-source-public-installed-targets',
        'compiler': subprocess.check_output(['clang', '--version'], text=True).strip()}
    (sdk / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')


if __name__ == '__main__':
    main()
