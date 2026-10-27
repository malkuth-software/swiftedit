"""Select a numeric version once before building, following PlaySuite publication."""
import json
import os
from pathlib import Path
import re

ROOT: Path = Path(__file__).resolve().parents[1]


def validate(version: str) -> list[int]:
    if re.fullmatch(r'(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)', version) is None:
        raise ValueError('Expected canonical major.minor.patch version')
    parts: list[int] = []
    value: str
    for value in version.split('.'):
        part: int = int(value)
        if part > 65535:
            raise ValueError('Version exceeds installer component range')
        parts.append(part)
    return parts


def baseline() -> str:
    source: str = (ROOT / 'CMakeLists.txt').read_text(encoding='utf-8')
    match: re.Match[str] | None = re.search(r'project\(SwiftEdit VERSION ([0-9.]+)', source)
    if match is None:
        raise ValueError('Missing SwiftEdit baseline version')
    return match.group(1)


def select(base: str, event: str, ref: str, run_number: int, requested: bool = False) -> dict[str, str]:
    parts: list[int] = validate(base)
    publish: bool = event == 'push' and (ref == 'refs/heads/master' or ref.startswith('refs/tags/v'))
    if event == 'workflow_dispatch' and requested:
        if ref != 'refs/heads/master':
            raise ValueError('Manual publication requires master')
        publish = True
    version: str = base
    if ref.startswith('refs/tags/'):
        if not ref.startswith('refs/tags/v'):
            raise ValueError('Expected a v-prefixed tag')
        version = ref[len('refs/tags/v'):]
        tagged: list[int] = validate(version)
        if tagged[:2] != parts[:2] or tagged[2] < parts[2]:
            raise ValueError('Tag must match baseline series and not predate it')
    elif publish:
        if run_number <= 0:
            raise ValueError('Publication requires a positive workflow run number')
        version = str(parts[0]) + '.' + str(parts[1]) + '.' + str(parts[2] + run_number)
    validate(version)
    result: dict[str, str] = {'version': version, 'tag': 'v' + version,
                              'publish': str(publish).lower()}
    return result


def project_version() -> str:
    version: str = os.environ.get('SWIFTEDIT_RELEASE_VERSION', baseline())
    validate(version)
    return version


def verify_build_version(build: Path) -> None:
    cache: str = (build / 'CMakeCache.txt').read_text(encoding='utf-8')
    match: re.Match[str] | None = re.search(r'^SWIFTEDIT_RELEASE_VERSION:STRING=(.+)$', cache, re.MULTILINE)
    if match is None or match.group(1).strip() != project_version():
        raise RuntimeError('Configured application version differs from the packaging version')


def main() -> None:
    selected: dict[str, str] = select(baseline(), os.environ.get('GITHUB_EVENT_NAME', 'local'),
        os.environ.get('GITHUB_REF', ''), int(os.environ.get('GITHUB_RUN_NUMBER', '0')),
        os.environ.get('PUBLISH_REQUESTED') == 'true')
    output: str | None = os.environ.get('GITHUB_OUTPUT')
    if output:
        with Path(output).open('a', encoding='utf-8') as stream:
            key: str
            value: str
            for key, value in selected.items():
                stream.write(key + '=' + value + '\n')
    print(json.dumps(selected))


if __name__ == '__main__':
    main()
