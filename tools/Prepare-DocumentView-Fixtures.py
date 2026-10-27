"""Create byte-exact fixtures for future installed DocumentView validation.

The output directory must not exist. These fixtures do not prove availability
or behavior of any GUI provider API; the manifest records source facts only.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
from typing import BinaryIO, Protocol


class Digest(Protocol):
    def update(self, data: bytes) -> None: ...
    def hexdigest(self) -> str: ...


def describe(path: Path, cases: list[str], probes: list[tuple[int, bytes]]) -> dict[str, object]:
    digest: Digest = hashlib.sha256()
    stream: BinaryIO
    with path.open('rb') as stream:
        chunk: bytes = stream.read(65536)
        while chunk:
            digest.update(chunk)
            chunk = stream.read(65536)
        offset: int
        expected: bytes
        for offset, expected in probes:
            stream.seek(offset)
            if stream.read(len(expected)) != expected:
                raise RuntimeError('Fixture probe mismatch: ' + path.name)
    probe_records: list[dict[str, object]] = []
    for offset, expected in probes:
        probe_records.append({'offset': offset, 'hex': expected.hex()})
    result: dict[str, object] = {
        'file': path.name, 'size': path.stat().st_size, 'sha256': digest.hexdigest(),
        'cases': cases, 'probes': probe_records}
    return result


def prepare(directory: Path) -> None:
    directory.mkdir()  # Never replace an existing fixture or user directory.
    records: list[dict[str, object]] = []
    source: bytes = (b'literal [U+0000] [U+202E] [FF]\r\n'
                     b'actual \x00\xe2\x80\xae\xff\xc0\xaf\r\n'
                     b'literal metadata \r\rL17\r\r\n'
                     b'combining e\xcc\x81 emoji \xf0\x9f\x98\x80\rbare\n')
    path: Path = directory / 'controls-and-bytes.txt'
    path.write_bytes(source)
    records.append(describe(path, ['literal labels versus actual controls',
        'invalid UTF-8 remains source bytes', 'mixed CRLF/CR/LF',
        'literal blank-line metadata markers', 'combining cluster and emoji'], [(0, source)]))

    boundary: bytearray = bytearray(b'x' * 131080)
    boundary[65535:65537] = b'\r\n'
    boundary[131071:131075] = b'\xf0\x9f\x98\x80'
    path = directory / 'page-boundaries.txt'
    path.write_bytes(boundary)
    records.append(describe(path, ['CRLF across 64 KiB boundary',
        'UTF-8 scalar across next 64 KiB boundary'],
        [(65534, b'x\r\nx'), (131070, b'x\xf0\x9f\x98\x80x')]))

    source = b'a' + b'\xcc\x81' * 35000 + b'\nshort\n'
    path = directory / 'long-grapheme.txt'
    path.write_bytes(source)
    records.append(describe(path, ['one grapheme exceeds 64 KiB context',
        'must not fabricate a boundary or silently drop source'], [(0, b'a\xcc\x81'),
        (70001, b'\nshort\n')]))

    source = b'word ' * 20000 + b'\r\nlast\n'
    path = directory / 'long-line.txt'
    path.write_bytes(source)
    records.append(describe(path, ['100000-byte logical line', 'bounded wrap/context behavior'],
        [(99995, b'word \r\nlast\n')]))

    path = directory / 'paged-read-only.txt'
    chunk: bytes = (b'0123456789abcdef' * 3 + b'0123456789abcd\r\n') * 1024
    if len(chunk) != 65536:
        raise RuntimeError('Large fixture chunk must be exactly 64 KiB')
    with path.open('wb') as stream:
        for index in range(256):
            stream.write(chunk)
        stream.write(b'last page\r\n')
    records.append(describe(path, ['above 16 MiB read-only threshold',
        'bounded first/middle/final page navigation'],
        [(0, chunk[:64]), (16 * 1024 * 1024, b'last page\r\n')]))

    for name, source in [('revision-a.txt', b'alpha\r\n'), ('revision-b.txt', b'bravo\r\n')]:
        path = directory / name
        path.write_bytes(source)
        records.append(describe(path, ['same-size different source for stale-token scenario'],
                                [(0, source)]))
    manifest: dict[str, object] = {'schema': 1, 'scope': 'source fixtures; no GUI proof',
        'files': records,
        'stale_token_scenario': 'Request A; replace/close its source; request B; deliver A late. '
        'Use the installed provider token contract to verify refusal and ownership cleanup.'}
    (directory / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    print('Created and byte-verified ' + str(len(records)) + ' fixtures in ' + str(directory))


def main() -> None:
    parser: argparse.ArgumentParser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    arguments: argparse.Namespace = parser.parse_args()
    prepare(arguments.directory)


if __name__ == '__main__':
    main()
