"""Publish complete native packages from exactly the tested source revision."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
from publication_version import project_version
from release_contract import checked_files, digest


def gh(arguments: list[str]) -> str:
    result: str = subprocess.check_output(['gh'] + arguments, text=True).strip()
    return result


def probe(arguments: list[str]) -> dict | None:
    observed: subprocess.CompletedProcess[str] = subprocess.run(['gh'] + arguments,
        text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
    if observed.returncode == 0:
        result: dict = json.loads(observed.stdout)
        return result
    if '404' not in observed.stderr:
        raise RuntimeError('Cannot verify publication state: ' + observed.stderr)
    return None


def current_master(repository: str, revision: str) -> bool:
    current: str = gh(['api', 'repos/' + repository + '/commits/master', '--jq', '.sha'])
    return current == revision


def main() -> None:
    parser: argparse.ArgumentParser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--artifacts', type=Path, required=True)
    parser.add_argument('--tag', required=True)
    parser.add_argument('--installers', action='store_true')
    arguments: argparse.Namespace = parser.parse_args()
    revision: str = subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True).strip()
    version: str = project_version()
    tag: str = arguments.tag
    if tag != 'v' + version:
        raise RuntimeError('Release tag must match the tested build version')
    repository: str = gh(['repo', 'view', '--json', 'nameWithOwner', '--jq', '.nameWithOwner'])
    if repository != 'falseywinchnet/swiftedit':
        raise RuntimeError('Unexpected publication destination: ' + repository)
    branch_publication: bool = os.environ.get('GITHUB_REF') == 'refs/heads/master'
    if branch_publication and not current_master(repository, revision):
        print('A newer master revision exists; retaining tested artifacts without publication.')
        return
    dependencies: dict = json.loads(Path('ci/dependencies.json').read_text(encoding='utf-8'))
    staging: Path = arguments.artifacts / 'publish'
    staging.mkdir(exist_ok=True)
    files: list[str] = []
    platform: str
    for platform in ['windows-x64', 'macos-arm64', 'linux-x64']:
        source: Path = arguments.artifacts / ('SwiftEdit-' + platform)
        item: Path
        for item in checked_files(source, platform, revision, version, dependencies, arguments.installers):
            name: str = platform + '-manifest.json' if item.suffix == '.json' else item.name
            target: Path = staging / name
            shutil.copy2(item, target)
            files.append(str(target))
    ref: dict | None = probe(['api', 'repos/' + repository + '/git/ref/tags/' + tag])
    if ref is None:
        if not branch_publication:
            raise RuntimeError('Tag publication requires an existing tested tag')
        gh(['api', 'repos/' + repository + '/git/refs', '--method', 'POST',
            '-f', 'ref=refs/tags/' + tag, '-f', 'sha=' + revision])
    if gh(['api', 'repos/' + repository + '/commits/' + tag, '--jq', '.sha']) != revision:
        raise RuntimeError('Release tag points at another source revision')
    notes: Path = staging / 'notes.md'
    changes_path: Path = Path('docs/releases') / (tag + '.md')
    if not changes_path.is_file():
        changes_path = Path('packaging/RELEASE_NOTES.md')
    changes: str = changes_path.read_text(encoding='utf-8') if changes_path.is_file() else ''
    notes.write_text('SwiftEdit ' + tag + ' for Windows, macOS and Linux, from commit `' + revision + '`.\n\n'
        + changes + '\n\nAll three native build/test jobs and package startup checks passed before publication.\n\n'
        '- **Windows:** x64 per-user setup executable, with Start menu shortcut and uninstall entry; portable ZIP also available. Unsigned.\n'
        '- **macOS:** Apple silicon `.pkg` installs SwiftEdit.app into Applications; portable ZIP also available. Built on macOS 15 using the GUI.Forms macOS 14 runtime contract. Ad-hoc signed app; installer unsigned and not notarized.\n'
        '- **Linux:** Ubuntu 24.04-compatible amd64 `.deb`, with desktop launcher and terminal commands; portable x64 tarball also available. X11 session required.\n\n'
        'Archives and installers include the GUI, terminal editor, CLI, runtime libraries, fonts and license notices. '
        'SHA-256 sidecars and exact source/dependency manifests accompany the downloads.\n', encoding='utf-8')
    existing: dict | None = probe(['api', 'repos/' + repository + '/releases/tags/' + tag])
    published: bool = existing is not None and not existing['draft']
    if existing is None:
        gh(['release', 'create', tag, '--repo', repository, '--verify-tag', '--target', revision, '--draft',
            '--title', 'SwiftEdit ' + tag + ' — Windows, macOS and Linux', '--notes-file', str(notes)])
    if not published:
        gh(['release', 'upload', tag, '--repo', repository, '--clobber'] + files)
    uploaded: dict = json.loads(gh(['release', 'view', tag, '--repo', repository, '--json', 'assets']))
    expected: dict[str, str] = {}
    filename: str
    for filename in files:
        expected[Path(filename).name] = 'sha256:' + digest(Path(filename))
    asset: dict
    for asset in uploaded['assets']:
        name = asset['name']
        if name not in expected or asset.get('digest') != expected[name]:
            raise RuntimeError('Uploaded release asset mismatch: ' + name)
        del expected[name]
    if expected:
        raise RuntimeError('Release assets are incomplete')
    if published:
        print('Matching tested release already published; leaving it immutable.')
        return
    if branch_publication and not current_master(repository, revision):
        print('A newer master revision exists; leaving this release as a draft.')
        return
    gh(['release', 'edit', tag, '--repo', repository, '--draft=false', '--latest'])


if __name__ == '__main__':
    main()
