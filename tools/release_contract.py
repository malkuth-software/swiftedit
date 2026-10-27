"""Validate a complete tested platform's archives and installer before publication."""
import hashlib
import json
from pathlib import Path


def digest(path: Path) -> str:
    with path.open('rb') as stream:
        result: str = hashlib.file_digest(stream, 'sha256').hexdigest()
    return result


def checked_files(source: Path, platform: str, revision: str, version: str,
                  dependencies: dict, installers: bool) -> list[Path]:
    suffix: str = '.tar.gz' if platform == 'linux-x64' else '.zip'
    archive: Path = source / ('SwiftEdit-' + platform + '-' + revision[:12] + suffix)
    manifest_path: Path = source / ('manifest.json' if platform == 'macos-arm64' else platform + '-manifest.json')
    manifest: dict = json.loads(manifest_path.read_text(encoding='utf-8'))
    if manifest['source_revision'] != revision or manifest.get('release_version') != version:
        raise RuntimeError('Mixed source/version in release: ' + platform)
    sdk: dict = manifest['sdk']
    if (sdk['provider_revision'] != dependencies['revision']
            or sdk['picker_revision'] != dependencies['picker_revision'] or sdk['platform'] != platform):
        raise RuntimeError('Unexpected dependencies in release: ' + platform)
    binaries: list[Path] = [archive]
    if installers:
        suffixes: dict[str, str] = {'windows-x64': '-windows-x64-setup.exe',
            'macos-arm64': '-macos-arm64.pkg', 'linux-x64': '-linux-amd64.deb'}
        name: str = 'SwiftEdit-' + version + suffixes[platform]
        receipt: dict = manifest.get('installer', {})
        installer: Path = source / name
        if (receipt.get('filename') != name or receipt.get('version') != version
                or not receipt.get('validation', '').endswith('passed')
                or receipt.get('sha256') != digest(installer)):
            raise RuntimeError('Installer validation receipt mismatch: ' + platform)
        binaries.append(installer)
    result: list[Path] = [manifest_path]
    binary: Path
    for binary in binaries:
        sidecar: Path = binary.with_name(binary.name + '.sha256')
        fields: list[str] = sidecar.read_text(encoding='utf-8').split()
        if fields != [digest(binary), binary.name]:
            raise RuntimeError('Package checksum mismatch: ' + str(binary))
        result.extend([binary, sidecar])
    return result
