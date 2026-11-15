"""A Linux package carries the programs alone: only system imports pass."""
import importlib.util
from pathlib import Path
import sys
import unittest

TOOLS: Path = Path(__file__).resolve().parents[1] / 'tools'
sys.path.insert(0, str(TOOLS))
SPEC = importlib.util.spec_from_file_location('package_desktop', TOOLS / 'Package-Desktop.py')
package_desktop = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(package_desktop)

READELF: str = (' 0x0000000000000001 (NEEDED)  Shared library: [libX11.so.6]\n'
                ' 0x0000000000000001 (NEEDED)  Shared library: [libatk-bridge-2.0.so.0]\n'
                ' 0x0000000000000001 (NEEDED)  Shared library: [libutil.so.1]\n'
                ' 0x0000000000000001 (NEEDED)  Shared library: [libc.so.6]\n')
LDD: str = ('\tlinux-vdso.so.1 (0x00007ffd4b5f2000)\n'
            '\tlibX11.so.6 => /lib/x86_64-linux-gnu/libX11.so.6 (0x00007f0a1c000000)\n'
            '\tlibxcb.so.1 => /usr/lib/x86_64-linux-gnu/libxcb.so.1 (0x00007f0a1bf00000)\n'
            '\t/lib64/ld-linux-x86-64.so.2 (0x00007f0a1c400000)\n')


class LinuxPackageTests(unittest.TestCase):
    def test_desktop_libraries_pass(self) -> None:
        self.assertEqual(package_desktop.linux_refusals(READELF, LDD), [])

    def test_bundled_or_missing_libraries_are_refused(self) -> None:
        needed: str = READELF + ' 0x0000000000000001 (NEEDED)  Shared library: [libmd4c.so.0]\n'
        ldd: str = LDD + '\tlibmd4c.so.0 => /usr/local/lib/libmd4c.so.0 (0x1)\n\tlibstx.so.1 => not found\n'
        refused: list[str] = package_desktop.linux_refusals(needed, ldd)
        self.assertEqual(refused[0], 'libmd4c.so.0')
        self.assertTrue(refused[1].startswith('libmd4c.so.0 => /usr/local/lib/'))
        self.assertEqual(refused[2], 'libstx.so.1 => not found')
        self.assertEqual(len(refused), 3)


if __name__ == '__main__':
    unittest.main()
