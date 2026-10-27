"""Build, test and package SwiftEdit with public packages from pinned source."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
from typing import BinaryIO, Protocol, TextIO
from publication_version import project_version


class Digest(Protocol):
    def update(self, data: bytes) -> None: ...
    def hexdigest(self) -> str: ...


def run(arguments: list[str]) -> None:
    print('+ ' + ' '.join(arguments), flush=True)
    subprocess.run(arguments, check=True)


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


def main() -> None:
    parser: argparse.ArgumentParser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--platform', choices=['macos-arm64', 'windows-x64', 'linux-x64'], required=True)
    parser.add_argument('--jobs', type=int, default=2)
    parser.add_argument('--installers', action='store_true')
    phase: argparse._MutuallyExclusiveGroup = parser.add_mutually_exclusive_group()
    phase.add_argument('--build-only', action='store_true')
    phase.add_argument('--test-package-only', action='store_true')
    arguments: argparse.Namespace = parser.parse_args()
    if arguments.jobs < 1:
        parser.error('--jobs must be positive')
    build: Path = Path('.build/native-' + arguments.platform + '/app').resolve()
    sdk: Path = build.parent / 'sdk'
    manifest: dict = json.loads((sdk / 'manifest.json').read_text(encoding='utf-8'))
    dependencies: dict = json.loads(Path('ci/dependencies.json').read_text())
    if (manifest['provider_revision'] != dependencies['revision']
            or manifest['picker_revision'] != dependencies['picker_revision']
            or manifest['platform'] != arguments.platform):
        raise RuntimeError('Installed packages differ from the dependency lock')
    lock: dict = {'provider_revision': dependencies['revision']}
    gui: Path = sdk / 'gui-forms-sdk'
    os.environ['GUI_FORMS_FONT_DIR'] = str(gui / 'share/GUIForms/fonts')
    os.environ['PATH'] = str(gui / 'bin') + os.pathsep + os.environ['PATH']
    os.environ['CCACHE_DIR'] = str(Path('.ccache').resolve())
    os.environ['CCACHE_BASEDIR'] = str(Path('.').resolve())
    os.environ['CCACHE_COMPILERCHECK'] = 'content'
    if arguments.platform == 'macos-arm64':
        os.environ['GUI_FORMS_LLVM_RUNTIME'] = str(Path('.build/toolchain/llvm-22.1.8-macos14').resolve())
    prefix: str = str(gui) + ';' + str(sdk / 'picker-sdk')
    if not arguments.test_package_only:
        run(['cmake', '-S', '.', '-B', str(build), '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release',
             '-DSWIFTEDIT_RELEASE_VERSION=' + project_version(),
             '-DCMAKE_PREFIX_PATH=' + prefix, '-DNOTEPAD_NATIVE_TESTS=ON',
             '-DCMAKE_TOOLCHAIN_FILE=' + str(Path('gui_forms/cmake/llvm22.cmake').resolve()),
             '-DCMAKE_C_COMPILER_LAUNCHER=ccache', '-DCMAKE_CXX_COMPILER_LAUNCHER=ccache',
             '-DCMAKE_OBJCXX_COMPILER_LAUNCHER=ccache'])
        run(['cmake', '--build', str(build), '--parallel', str(arguments.jobs)])
    if arguments.build_only:
        return
    run(['ctest', '--test-dir', str(build), '--output-on-failure', '--timeout', '60'])
    benchmark: Path = build / ('swiftedit-terminal-end-bench.exe' if os.name == 'nt'
                               else 'swiftedit-terminal-end-bench')
    evidence: Path = build / 'terminal-end-evidence'
    evidence.mkdir(exist_ok=True)
    summary: TextIO
    with (evidence / 'summary.txt').open('w', encoding='utf-8') as summary:
        measured: subprocess.CompletedProcess[bytes] = subprocess.run(
            [str(benchmark), str(evidence / 'fixtures')], check=False, timeout=120,
            stdout=summary, stderr=subprocess.STDOUT)
    measured.check_returncode()
    revision_output: str = subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True)
    revision: str = revision_output.strip()
    receipt: dict[str, object] = {
        'source_revision': revision,
        'provider_revision': lock['provider_revision'], 'platform': arguments.platform,
        'executable_sha256': sha256(benchmark), 'status': 'completed',
        'scope': 'headless read-only terminal end scan; not native input-to-screen latency',
        'fixture_bytes': 16777216, 'viewport': [80, 24], 'trials_per_fixture': 8,
        'source_budgets': [8192, 65536], 'trials_per_budget': 4,
        'budget_order': [8192, 65536, 65536, 8192, 8192, 65536, 65536, 8192],
        'percentiles': 'nearest rank; per-step samples pooled across trials',
        'clock_overhead': 'included; step wall intervals include process CPU clock calls',
        'cpu_clock': 'GetProcessTimes on Windows, std::clock on POSIX; process-wide; resolution varies',
        'cache_state': 'not controlled; trials run sequentially',
        'performance_threshold': 'none'}
    (evidence / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n', encoding='utf-8')
    search_benchmark: Path = build / ('swiftedit-search-bench.exe' if os.name == 'nt'
                                      else 'swiftedit-search-bench')
    search_evidence: Path = build / 'search-evidence'
    search_evidence.mkdir(exist_ok=True)
    with (search_evidence / 'summary.txt').open('w', encoding='utf-8') as summary:
        measured = subprocess.run(
            [str(search_benchmark), str(search_evidence / 'fixtures')], check=False,
            timeout=120, stdout=summary, stderr=subprocess.STDOUT)
    measured.check_returncode()
    search_receipt: dict[str, object] = {
        'source_revision': revision, 'provider_revision': lock['provider_revision'],
        'platform': arguments.platform, 'executable_sha256': sha256(search_benchmark),
        'status': 'completed',
        'scope': 'headless SessionSearch steps; not native input-to-screen or idle CPU',
        'fixture_bytes': 16777216, 'trials_per_mode': 3,
        'modes': ['literal Z', 'one-grapheme wildcard followed by literal Z'],
        'comparison_work_budget': 4096, 'ordinary_source_context': 8192,
        'percentiles': 'nearest rank; per-step samples pooled across trials',
        'clock_overhead': 'included; step wall intervals include process CPU clock calls',
        'cpu_clock': 'GetProcessTimes on Windows, std::clock on POSIX; process-wide; resolution varies',
        'cache_state': 'not controlled; literal then wildcard, sequential trials',
        'performance_threshold': 'none',
        'limits': 'three simple-pattern fixtures; not exhaustive query-complexity coverage'}
    (search_evidence / 'receipt.json').write_text(json.dumps(search_receipt, indent=2) + '\n', encoding='utf-8')
    copy_benchmark: Path = build / ('swiftedit-text-copy-bench.exe' if os.name == 'nt'
                                    else 'swiftedit-text-copy-bench')
    copy_evidence: Path = build / 'text-copy-evidence'
    copy_evidence.mkdir(exist_ok=True)
    copy_receipt: dict[str, object] = {
        'source_revision': revision, 'provider_revision': lock['provider_revision'],
        'platform': arguments.platform, 'executable_sha256': sha256(copy_benchmark),
        'status': 'started',
        'scope': 'headless SessionTextCopy stages; not physical input latency or idle CPU',
        'fixture_bytes': 16777216, 'trials_per_fixture': 3, 'source_step_bytes': 65536,
        'fixtures': ['ASCII', 'Unicode', 'invalid UTF-8'],
        'operations': ['prepare', 'step', 'dispatch', 'publication total', 'completion', 'cancel'],
        'validation': 'every completed output compared byte-for-byte with whole-source conversion; cancelled destination absent; source stamp unchanged',
        'percentiles': 'nearest rank; steps pooled across trials; other phases only three samples per fixture',
        'clock': 'steady_clock wall; GetProcessTimes Windows or std::clock POSIX process CPU',
        'cpu_fields': 'measured for step/publication total; empty elsewhere; coarse accounting may report zero',
        'cache_state': 'uncontrolled; ASCII then Unicode then invalid; validation after each trial',
        'completion_wait_ms': 8, 'process_timeout_seconds': 120,
        'performance_threshold': 'none'}
    try:
        with (copy_evidence / 'summary.txt').open('w', encoding='utf-8') as summary:
            measured = subprocess.run(
                [str(copy_benchmark), str(copy_evidence / 'fixtures')], check=False,
                timeout=120, stdout=summary, stderr=subprocess.STDOUT)
        copy_receipt['exit_code'] = measured.returncode
        copy_receipt['status'] = 'completed' if measured.returncode == 0 else 'failed'
        measured.check_returncode()
    except subprocess.TimeoutExpired:
        copy_receipt['status'] = 'timed_out'
        raise
    finally:
        (copy_evidence / 'receipt.json').write_text(json.dumps(copy_receipt, indent=2) + '\n', encoding='utf-8')
    navigation_benchmark: Path = build / ('swiftedit-terminal-navigation-bench.exe' if os.name == 'nt'
                                          else 'swiftedit-terminal-navigation-bench')
    navigation_evidence: Path = build / 'navigation-evidence'
    navigation_evidence.mkdir(exist_ok=True)
    navigation_receipt: dict[str, object] = {
        'source_revision': revision, 'provider_revision': lock['provider_revision'],
        'platform': arguments.platform, 'executable_sha256': sha256(navigation_benchmark),
        'status': 'started', 'trials_per_fixture': 5, 'viewport': [80, 24],
        'repeated_moves_each_direction': 1000, 'maximum_moves_per_slice': 16,
        'scope': 'headless shared-loop navigation to screen-string submission; excludes OS input delivery and physical presentation',
        'clock': 'steady_clock wall time; sample-vector insertion excluded',
        'percentiles': 'nearest rank, pooled slices across trials and both directions',
        'cache_state': 'uncontrolled; sequential ASCII, Unicode, control/invalid-byte fixtures',
        'performance_threshold': 'none'}
    try:
        with (navigation_evidence / 'summary.txt').open('w', encoding='utf-8') as summary:
            measured = subprocess.run(
                [str(navigation_benchmark), str(navigation_evidence / 'fixtures')],
                check=False, timeout=120, stdout=summary, stderr=subprocess.STDOUT)
        navigation_receipt['exit_code'] = measured.returncode
        navigation_receipt['status'] = 'completed' if measured.returncode == 0 else 'failed'
        measured.check_returncode()
    except subprocess.TimeoutExpired:
        navigation_receipt['status'] = 'timed_out'
        raise
    finally:
        (navigation_evidence / 'receipt.json').write_text(json.dumps(navigation_receipt, indent=2) + '\n', encoding='utf-8')
    package: list[str] = [sys.executable, '-B', 'tools/Package-Mac.py', '--build', str(build), '--sdk', str(sdk)]
    if arguments.platform != 'macos-arm64':
        package = [sys.executable, '-B', 'tools/Package-Desktop.py', '--build', str(build),
                   '--sdk', str(sdk), '--platform', arguments.platform]
    if arguments.installers:
        package.append('--installer')
    run(package)


if __name__ == '__main__':
    main()
