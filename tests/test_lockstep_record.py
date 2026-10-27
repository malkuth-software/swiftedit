"""A lockstep record is admitted only for its own target and schema."""
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from prepare_dependencies import LOCKSTEP_VARIABLE, read_lockstep


class LockstepRecord(unittest.TestCase):
    def setUp(self) -> None:
        self.saved: str | None = os.environ.pop(LOCKSTEP_VARIABLE, None)

    def tearDown(self) -> None:
        os.environ.pop(LOCKSTEP_VARIABLE, None)
        if self.saved is not None:
            os.environ[LOCKSTEP_VARIABLE] = self.saved

    def named(self, root: Path, record: dict) -> None:
        path: Path = root / 'current.json'
        path.write_text(json.dumps(record), encoding='utf-8')
        os.environ[LOCKSTEP_VARIABLE] = str(path)

    def test_unset_means_pinned(self) -> None:
        self.assertIsNone(read_lockstep('windows-x64'))

    def test_matching_record_admitted(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            self.named(Path(temporary), {'schema': 1, 'target': 'windows-x64', 'commit': 'reviewed'})
            record: dict | None = read_lockstep('windows-x64')
            self.assertIsNotNone(record)
            self.assertEqual(record['commit'], 'reviewed')

    def test_other_target_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            self.named(Path(temporary), {'schema': 1, 'target': 'linux-x64'})
            with self.assertRaises(ValueError):
                read_lockstep('windows-x64')

    def test_other_schema_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            self.named(Path(temporary), {'schema': 2, 'target': 'windows-x64'})
            with self.assertRaises(ValueError):
                read_lockstep('windows-x64')


if __name__ == '__main__':
    unittest.main()
