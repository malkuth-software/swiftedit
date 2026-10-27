"""Export the existing valid focused Time Profiler recording; never recapture."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import xml.etree.ElementTree as ET


def main() -> None:
    parser: argparse.ArgumentParser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    arguments: argparse.Namespace = parser.parse_args()
    attempt: Path = arguments.input / 'attempt-076b0fda4a7943d1b97fffc20b12c0d8'
    source: Path = attempt / 'receipt.json'
    receipt: dict[str, object] = json.loads(source.read_text(encoding='utf-8'))
    focused: dict[str, object] | None = None
    run: dict[str, object]
    for run in receipt['runs']:
        if run['mode'] == 'focused' and run['profiled']:
            focused = run
    if focused is None or focused['status'] != 'completed':
        raise RuntimeError('The recorded focused workload is not valid')
    workload: dict[str, object] = focused['workload']
    seconds: float = float(workload['span']['wall_seconds'])
    if workload['status'] != 'completed' or not 120 <= seconds <= 130:
        raise RuntimeError('Focused workload failed its original duration gate')
    trace_directory: Path = attempt / '02-profiled-focused'
    toc: ET.Element = ET.parse(trace_directory / 'toc.xml').getroot()
    arguments.output.mkdir(parents=True)
    exports: list[dict[str, object]] = []
    summary: dict[str, object] = {
        'provider_run': 36963650387,
        'attempt': attempt.name,
        'input_receipt_sha256': hashlib.sha256(source.read_bytes()).hexdigest(),
        'executable_sha256': focused['executable_sha256'],
        'workload': focused['workload'],
        'comparison_accepted': False,
        'scope': 'existing focused trace only; cleared workloads failed their duration gate',
        'analysis': 'pending sample alignment, symbol resolution and sufficiency checks',
        'exports': exports}
    schemas: list[str] = ['time-profile', 'time-sample', 'os-signpost',
                          'os-signpost-arg', 'process-info', 'thread-info', 'dyld-library-load']
    try:
        schema: str
        for schema in schemas:
            table: str = 'table[@schema="' + schema + '"]'
            if schema == 'os-signpost':
                table += '[@category="PointsOfInterest"]'
            selector: str = './run[@number="1"]/data/' + table
            if len(toc.findall(selector)) != 1:
                raise RuntimeError('Expected one unambiguous recorded table: ' + schema)
            output: Path = arguments.output / (schema + '.xml')
            command: list[str] = ['xcrun', 'xctrace', 'export', '--input',
                                  str(trace_directory / 'recording.trace'), '--xpath',
                                  '/trace-toc/run[@number="1"]/data/' + table,
                                  '--output', str(output)]
            exported: dict[str, object] = {'schema': schema, 'status': 'started'}
            exports.append(exported)
            try:
                with (arguments.output / (schema + '.log')).open('w', encoding='utf-8') as log:
                    result: subprocess.CompletedProcess[str] = subprocess.run(
                        command, text=True, stdout=log, stderr=subprocess.STDOUT, timeout=120, check=False)
            except subprocess.TimeoutExpired:
                exported['status'] = 'timed_out'
                raise
            exported['exit_code'] = result.returncode
            exported['status'] = 'exported' if result.returncode == 0 else 'failed'
            result.check_returncode()
            with output.open('rb') as stream:
                exported['sha256'] = hashlib.file_digest(stream, 'sha256').hexdigest()
            exported['bytes'] = output.stat().st_size
    finally:
        (arguments.output / 'receipt.json').write_text(json.dumps(summary, indent=2) + '\n', encoding='utf-8')


if __name__ == '__main__':
    main()
