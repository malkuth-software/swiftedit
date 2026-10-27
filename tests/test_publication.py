"""Publication must reject mixed, partial or untested installer payloads."""
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from publication_version import select, validate
from release_contract import checked_files, digest
from installers import windows_script, build_and_verify, nsis_command
from unittest.mock import patch


class PublicationTests(unittest.TestCase):
    def test_master_selects_version_before_build_and_reruns_keep_it(self) -> None:
        selected: dict = select('0.3.10', 'push', 'refs/heads/master', 40)
        self.assertEqual(selected, {'version': '0.3.50', 'tag': 'v0.3.50', 'publish': 'true'})
        self.assertEqual(selected, select('0.3.10', 'push', 'refs/heads/master', 40))

    def test_pull_requests_and_manual_dry_runs_do_not_publish(self) -> None:
        self.assertEqual(select('0.3.10', 'pull_request', 'refs/pull/7/merge', 40)['publish'], 'false')
        self.assertEqual(select('0.3.10', 'workflow_dispatch', 'refs/heads/master', 40)['publish'], 'false')
        self.assertEqual(select('0.3.10', 'workflow_dispatch', 'refs/heads/master', 40, True)['publish'], 'true')
        with self.assertRaises(ValueError):
            select('0.3.10', 'workflow_dispatch', 'refs/heads/feature', 40, True)

    def test_tag_and_installer_version_boundaries(self) -> None:
        self.assertEqual(select('0.3.10', 'push', 'refs/tags/v0.3.12', 40)['version'], '0.3.12')
        value: str
        for value in ['0.3.09', '0.3.65536', '0.3.10-beta', '0.3.10\n']:
            with self.assertRaises(ValueError):
                validate(value)
        with self.assertRaises(ValueError):
            select('0.3.10', 'push', 'refs/tags/v0.3.9', 40)
        with self.assertRaises(ValueError):
            select('0.3.10', 'push', 'refs/heads/master', 65535)

    def fixture(self, root: Path) -> tuple[Path, dict]:
        archive: Path = root / ('SwiftEdit-windows-x64-' + 'a' * 12 + '.zip')
        installer: Path = root / 'SwiftEdit-0.3.50-windows-x64-setup.exe'
        binary: Path
        for binary in [archive, installer]:
            binary.write_bytes(b'fixture payload')
            binary.with_name(binary.name + '.sha256').write_text(digest(binary) + '  ' + binary.name + '\n')
        manifest: dict = {'source_revision': 'a' * 40, 'release_version': '0.3.50',
            'sdk': {'provider_revision': 'toolkit', 'picker_revision': 'picker', 'platform': 'windows-x64'},
            'installer': {'filename': installer.name, 'version': '0.3.50',
                          'sha256': digest(installer), 'validation': 'installed startup passed'}}
        path: Path = root / 'windows-x64-manifest.json'
        path.write_text(json.dumps(manifest))
        return path, manifest

    def check(self, root: Path) -> list[Path]:
        return checked_files(root, 'windows-x64', 'a' * 40, '0.3.50',
                             {'revision': 'toolkit', 'picker_revision': 'picker'}, True)

    def test_complete_release_and_tampered_installer(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root: Path = Path(temporary)
            self.fixture(root)
            self.assertEqual(len(self.check(root)), 5)
            (root / 'SwiftEdit-0.3.50-windows-x64-setup.exe').write_bytes(b'tampered')
            with self.assertRaises(RuntimeError):
                self.check(root)

    def test_reject_missing_installer_receipt_and_mixed_revision(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root: Path = Path(temporary)
            path: Path
            manifest: dict
            path, manifest = self.fixture(root)
            manifest.pop('installer')
            path.write_text(json.dumps(manifest))
            with self.assertRaises(RuntimeError):
                self.check(root)
            path, manifest = self.fixture(root)
            manifest['source_revision'] = 'b' * 40
            path.write_text(json.dumps(manifest))
            with self.assertRaises(RuntimeError):
                self.check(root)
            path, manifest = self.fixture(root)
            manifest['release_version'] = '0.3.49'
            path.write_text(json.dumps(manifest))
            with self.assertRaises(RuntimeError):
                self.check(root)

    def test_uninstaller_names_owned_files_without_recursive_removal(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root: Path = Path(temporary)
            (root / 'fonts').mkdir()
            (root / 'fonts/example.ttf').write_bytes(b'font')
            script: str = windows_script(root, root / 'setup.exe', '0.3.50')
            self.assertIn('Delete "$INSTDIR\\fonts\\example.ttf"', script)
            self.assertNotIn('RMDir /r', script)
            self.assertNotIn('Delete "$INSTDIR\\*', script)

    def test_installation_is_not_started_on_developer_machine(self) -> None:
        with patch.dict('os.environ', {'GITHUB_ACTIONS': 'false'}):
            with self.assertRaises(RuntimeError):
                build_and_verify(Path('unused'), Path('unused'), 'windows-x64')

    def test_nsis_converts_msys_forward_slash_paths(self) -> None:
        script: str = windows_script(Path('D:/fixture/app'), Path('D:/fixture/setup.exe'), '0.3.50')
        self.assertIn('File /r "D:\\fixture\\app\\*"', script)
        self.assertIn('OutFile "D:\\fixture\\setup.exe"', script)

    def test_nsis_directory_tail_is_unquoted_even_with_spaces(self) -> None:
        directory: Path = Path('D:/fixture/user folder')
        installer: str = nsis_command(Path('D:/tools with spaces/setup.exe'), directory, False)
        uninstaller: str = nsis_command(Path('D:/tools/uninstall.exe'), directory, True)
        self.assertTrue(installer.startswith('"D:\\tools with spaces\\setup.exe" /S '))
        self.assertTrue(installer.endswith(' /D=D:\\fixture\\user folder'))
        self.assertTrue(uninstaller.endswith(' _?=D:\\fixture\\user folder'))


if __name__ == '__main__':
    unittest.main()
