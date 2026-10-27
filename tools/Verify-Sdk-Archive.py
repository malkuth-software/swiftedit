"""Read-only verification of a downloaded provider SDK and its content receipt."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path, PurePath, PurePosixPath, PureWindowsPath
from typing import BinaryIO, Protocol
import zipfile


class Digest(Protocol):
    def update(self, data: bytes) -> None: ...
    def hexdigest(self) -> str: ...


def hash_stream(stream: BinaryIO, digest: Digest) -> None:
    data: bytes = stream.read(1024 * 1024)
    while data:
        digest.update(data)
        data = stream.read(1024 * 1024)


def main() -> None:
    parser: argparse.ArgumentParser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('archive', type=Path)
    parser.add_argument('--revision', required=True)
    parser.add_argument('--platform', required=True)
    arguments: argparse.Namespace = parser.parse_args()
    archive: Path = arguments.archive
    digest: Digest = hashlib.sha256()
    stream: BinaryIO
    with archive.open('rb') as stream:
        hash_stream(stream, digest)
    archive_hash: str = digest.hexdigest()
    expected: str = archive.with_suffix('.zip.sha256').read_text(encoding='utf-8').split()[0]
    if archive_hash != expected:
        raise RuntimeError('Archive does not match its SHA256 receipt')
    package: zipfile.ZipFile
    with zipfile.ZipFile(archive) as package:
        manifest: dict[str, object] = json.loads(package.read('manifest.json'))
        if manifest['provider_revision'] != arguments.revision or manifest['platform'] != arguments.platform:
            raise RuntimeError('Wrong provider revision or platform')
        # Match the provider filesystem's component-wise path ordering.
        path_type: type[PurePath] = PurePosixPath
        if arguments.platform.startswith('windows-'):
            path_type = PureWindowsPath
        paths: list[PurePath] = []
        entry: zipfile.ZipInfo
        for entry in package.infolist():
            if not entry.is_dir():
                paths.append(path_type(entry.filename))
        paths.sort()
        sdk: str
        field: str
        for sdk, field in [('gui-forms-sdk', 'gui_forms_sha256'), ('picker-sdk', 'picker_sha256')]:
            digest = hashlib.sha256()
            directory: str
            path: PurePath
            for directory in ['include', 'lib', 'bin', 'share']:
                prefix: str = sdk + '/' + directory + '/'
                for path in paths:
                    name: str = path.as_posix()
                    if name.startswith(prefix):
                        relative: str = name[len(sdk) + 1:]
                        digest.update(relative.encode('utf-8'))
                        digest.update(b'\0')
                        with package.open(name) as stream:
                            hash_stream(stream, digest)
                        digest.update(b'\0')
            if digest.hexdigest() != manifest[field]:
                raise RuntimeError('Installed content differs from receipt: ' + sdk)
        print(json.dumps(manifest, indent=2))
    print('Verified archive SHA256: ' + archive_hash)


if __name__ == '__main__':
    main()
