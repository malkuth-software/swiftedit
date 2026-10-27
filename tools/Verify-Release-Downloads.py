"""Verify all three downloaded release archives without extracting or executing them.

Checks archive sidecars, exact source/SDK identity, and every manifest-listed
file hash (the three executable hashes on macOS). Writes a receipt only after
all platforms pass. This does not independently certify unlisted bundle files.
"""
from pathlib import Path
import argparse
import hashlib
import json
import tarfile
import zipfile


def main() -> None:
    parser: argparse.ArgumentParser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--downloads', type=Path, required=True)
    parser.add_argument('--revision', required=True)
    parser.add_argument('--provider', required=True)
    arguments: argparse.Namespace = parser.parse_args()
    root: Path = arguments.downloads
    revision: str = arguments.revision
    provider: str = arguments.provider
    if len(revision) != 40:
        raise ValueError('Expected a full lowercase source commit SHA')
    character: str
    for character in revision:
        if character not in '0123456789abcdef':
            raise ValueError('Expected a full lowercase source commit SHA')
    report: dict[str, object] = {'revision': revision, 'provider_revision': provider, 'platforms': {}}
    platform: str
    for platform in ['windows-x64', 'linux-x64', 'macos-arm64']:
        suffix: str = '.tar.gz' if platform == 'linux-x64' else '.zip'
        archive: Path = root / ('SwiftEdit-' + platform + '-' + revision[:12] + suffix)
        with archive.open('rb') as stream:
            digest: str = hashlib.file_digest(stream, 'sha256').hexdigest()
        sidecar: str = archive.with_name(archive.name + '.sha256').read_text().split()[0]
        if digest != sidecar:
            raise RuntimeError('Archive hash mismatch: ' + platform)
        manifest: dict = json.loads((root / (platform + '-manifest.json')).read_text())
        if manifest['source_revision'] != revision or manifest['sdk']['provider_revision'] != provider:
            raise RuntimeError('Unexpected source or SDK revision: ' + platform)
        if platform != 'macos-arm64':
            executable_suffix: str = '.exe' if platform == 'windows-x64' else ''
            executable: str
            for executable in ['SwiftEdit', 'swiftedit-terminal', 'swiftedit-cli']:
                if executable + executable_suffix not in manifest['files']:
                    raise RuntimeError('Manifest omits executable: ' + executable)
        checked: int = 0
        name: str
        expected: str
        if platform == 'linux-x64':
            with tarfile.open(archive, 'r:gz') as bundle:
                for name, expected in manifest['files'].items():
                    with bundle.extractfile('SwiftEdit/' + name) as stream:
                        actual: str = hashlib.file_digest(stream, 'sha256').hexdigest()
                    if actual != expected:
                        raise RuntimeError('Packaged file mismatch: ' + name)
                    checked += 1
        else:
            with zipfile.ZipFile(archive) as bundle:
                hashes: dict[str, str] = {}
                if platform == 'macos-arm64':
                    hashes = {
                        'SwiftEdit.app/Contents/MacOS/SwiftEdit': manifest['executable_sha256'],
                        'SwiftEdit.app/Contents/MacOS/SwiftEdit-terminal': manifest['terminal_sha256'],
                        'SwiftEdit.app/Contents/MacOS/swiftedit-cli': manifest['cli_sha256']}
                else:
                    for name, expected in manifest['files'].items():
                        hashes['SwiftEdit/' + name] = expected
                for name, expected in hashes.items():
                    with bundle.open(name) as stream:
                        actual = hashlib.file_digest(stream, 'sha256').hexdigest()
                    if actual != expected:
                        raise RuntimeError('Packaged file mismatch: ' + name)
                    checked += 1
        report['platforms'][platform] = {'archive_sha256': digest, 'manifest_file_hashes_checked': checked}
    output: str = json.dumps(report, indent=2)
    (root / 'independent-verification.json').write_text(output + '\n', encoding='utf-8')
    print(output)


if __name__ == "__main__":
    main()
