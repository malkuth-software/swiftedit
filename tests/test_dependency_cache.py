"""Provider caches must be authenticated before any local state is changed."""
import hashlib
import io
import json
from pathlib import Path
import sys
import tarfile
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from prepare_dependencies import import_cache


class CacheAdmission(unittest.TestCase):
    def fixture(self, root: Path, entry: str = '.ccache/object', revision: str = 'reviewed') -> tuple[Path, str]:
        archive: Path = root / 'cache.tar.gz'
        manifest: bytes = json.dumps({'repository': 'falseywinchnet/gui_forms',
            'revision': revision, 'platform': 'windows-x64'}).encode()
        with tarfile.open(archive, 'w:gz') as bundle:
            name: str
            data: bytes
            for name, data in [('cache-manifest.json', manifest), (entry, b'object')]:
                member: tarfile.TarInfo = tarfile.TarInfo(name)
                member.size = len(data)
                bundle.addfile(member, io.BytesIO(data))
        digest: str = hashlib.sha256(archive.read_bytes()).hexdigest()
        return archive, digest

    def test_valid_import(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root: Path = Path(temporary)
            archive, digest = self.fixture(root)
            import_cache(archive, root / 'out', digest, 'reviewed', 'windows-x64')
            self.assertEqual((root / 'out/.ccache/object').read_bytes(), b'object')

    def test_digest_rejected_before_write(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root: Path = Path(temporary)
            archive, digest = self.fixture(root)
            with self.assertRaises(ValueError):
                import_cache(archive, root / 'out', '0' * 64, 'reviewed', 'windows-x64')
            self.assertFalse((root / 'out').exists())

    def test_identity_rejected_before_write(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root: Path = Path(temporary)
            archive, digest = self.fixture(root, revision='other')
            with self.assertRaises(ValueError):
                import_cache(archive, root / 'out', digest, 'reviewed', 'windows-x64')
            self.assertFalse((root / 'out').exists())

    def test_traversal_rejected_before_write(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root: Path = Path(temporary)
            archive, digest = self.fixture(root, entry='.ccache/../../escaped')
            with self.assertRaises(ValueError):
                import_cache(archive, root / 'out', digest, 'reviewed', 'windows-x64')
            self.assertFalse((root / 'out').exists())
            self.assertFalse((root / 'escaped').exists())

    def test_cache_cannot_overwrite_consumer_source(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root: Path = Path(temporary)
            archive, digest = self.fixture(root, entry='.ccache/../src/consumer.cpp')
            with self.assertRaises(ValueError):
                import_cache(archive, root / 'out', digest, 'reviewed', 'windows-x64')
            self.assertFalse((root / 'out').exists())


if __name__ == '__main__':
    unittest.main()
