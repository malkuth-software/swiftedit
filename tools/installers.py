"""Build native installers; exercise installation only on dedicated CI runners."""
import hashlib
import os
from pathlib import Path
import plistlib
import shutil
import subprocess
import tempfile
from publication_version import project_version


def run(arguments: list[str], cwd: Path | None = None) -> str:
    result: subprocess.CompletedProcess[str] = subprocess.run(arguments, cwd=cwd,
        text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=180)
    print(result.stdout, end='', flush=True)
    result.check_returncode()
    return result.stdout


def sha256(path: Path) -> str:
    with path.open('rb') as stream:
        result: str = hashlib.file_digest(stream, 'sha256').hexdigest()
    return result


def smoke(executable: Path, cli: Path, directory: Path) -> None:
    environment: dict[str, str] = dict(os.environ)
    variable: str
    for variable in ['GUI_FORMS_FONT_DIR', 'LD_LIBRARY_PATH', 'DYLD_LIBRARY_PATH', 'DYLD_FALLBACK_LIBRARY_PATH']:
        environment.pop(variable, None)
    if os.name == 'nt':
        environment['PATH'] = str(Path(os.environ['WINDIR']) / 'System32')
    response: subprocess.CompletedProcess[str] = subprocess.run([str(cli)],
        input='info\nquit\n', env=environment, cwd=directory, text=True,
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=True, timeout=15)
    if 'ready\tSwiftEdit\t1' not in response.stdout or 'ok\tquit' not in response.stdout:
        raise RuntimeError('Installed CLI smoke failed: ' + response.stdout)
    with (directory / 'installed-startup.log').open('wb') as stream:
        child: subprocess.Popen[bytes] = subprocess.Popen([str(executable)], cwd=directory,
            env=environment, stdout=stream, stderr=stream)
        try:
            try:
                child.wait(timeout=3)
                raise RuntimeError('Installed GUI exited during startup')
            except subprocess.TimeoutExpired:
                pass
        finally:
            if child.poll() is None:
                child.terminate()
                try:
                    child.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    child.kill()
                    child.wait(timeout=5)


def verify_payload(source: Path, installed: Path) -> None:
    path: Path
    for path in source.rglob('*'):
        if path.is_file():
            target: Path = installed / path.relative_to(source)
            if not target.is_file():
                raise RuntimeError('Installer did not create expected file: ' + str(target))
            if sha256(path) != sha256(target):
                raise RuntimeError('Installed payload mismatch: ' + str(target))


def nsis_quote(value: str) -> str:
    result: str = value.replace('$', '$$').replace('"', '$\\"')
    return result


def nsis_command(executable: Path, directory: Path, uninstall: bool) -> str:
    # NSIS consumes the entire unquoted tail after /D= or _?=, including spaces.
    # https://nsis.sourceforge.io/Docs/Chapter3.html#installerusage
    option: str = ' _?=' if uninstall else ' /D='
    native: str = str(directory).replace('/', '\\')
    if '"' in native or '\n' in native or '\r' in native:
        raise ValueError('Invalid native installation directory')
    executable_path: str = str(executable).replace('/', '\\')
    command: str = subprocess.list2cmdline([executable_path, '/S']) + option + native
    return command


def invoke_nsis(executable: Path, directory: Path, uninstall: bool = False) -> None:
    command: str = nsis_command(executable, directory, uninstall)
    # Pass a native command line directly to CreateProcess, never through a shell.
    subprocess.run(command, shell=False, check=True, timeout=180)


def windows_script(bundle: Path, installer: Path, version: str) -> str:
    uninstall_key: str = 'Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\SwiftEdit'
    lines: list[str] = ['Unicode True', '!include "MUI2.nsh"', 'Name "SwiftEdit"',
        'OutFile "' + nsis_quote(str(installer).replace('/', '\\')) + '"',
        'InstallDir "$LOCALAPPDATA\\Programs\\SwiftEdit"',
        'InstallDirRegKey HKCU "' + uninstall_key + '" "InstallLocation"',
        'RequestExecutionLevel user', 'VIProductVersion "' + version + '.0"',
        'VIAddVersionKey "ProductName" "SwiftEdit"',
        'VIAddVersionKey "FileDescription" "SwiftEdit Installer"',
        'VIAddVersionKey "FileVersion" "' + version + '"',
        '!insertmacro MUI_PAGE_WELCOME', '!insertmacro MUI_PAGE_DIRECTORY',
        '!insertmacro MUI_PAGE_INSTFILES', '!insertmacro MUI_PAGE_FINISH',
        '!insertmacro MUI_UNPAGE_CONFIRM', '!insertmacro MUI_UNPAGE_INSTFILES',
        '!insertmacro MUI_LANGUAGE "English"', 'Section "SwiftEdit"',
        'SetShellVarContext current', 'SetOutPath "$INSTDIR"',
        'File /r "' + nsis_quote(str(bundle / '*').replace('/', '\\')) + '"',
        'CreateDirectory "$SMPROGRAMS\\SwiftEdit"',
        'CreateShortcut "$SMPROGRAMS\\SwiftEdit\\SwiftEdit.lnk" "$INSTDIR\\SwiftEdit.exe"',
        'WriteUninstaller "$INSTDIR\\Uninstall.exe"',
        'WriteRegStr HKCU "' + uninstall_key + '" "DisplayName" "SwiftEdit"',
        'WriteRegStr HKCU "' + uninstall_key + '" "DisplayVersion" "' + version + '"',
        'WriteRegStr HKCU "' + uninstall_key + '" "InstallLocation" "$INSTDIR"',
        'WriteRegStr HKCU "' + uninstall_key + '" "DisplayIcon" "$INSTDIR\\SwiftEdit.exe"',
        'WriteRegStr HKCU "' + uninstall_key + '" "UninstallString" \'$\\"$INSTDIR\\Uninstall.exe$\\"\'',
        'WriteRegDWORD HKCU "' + uninstall_key + '" "NoModify" 1',
        'WriteRegDWORD HKCU "' + uninstall_key + '" "NoRepair" 1',
        'SectionEnd', 'Section "Uninstall"', 'SetShellVarContext current']
    directories: list[str] = []
    path: Path
    for path in sorted(bundle.rglob('*')):
        relative: str = nsis_quote(str(path.relative_to(bundle)).replace('/', '\\'))
        if path.is_file():
            lines.append('Delete "$INSTDIR\\' + relative + '"')
        elif path.is_dir():
            directories.append(relative)
    # Only remove package-owned files and empty directories, never a recursive
    # user-selected install directory or documents written there after install.
    for relative in sorted(directories, key=len, reverse=True):
        lines.append('RMDir "$INSTDIR\\' + relative + '"')
    lines.extend(['Delete "$INSTDIR\\Uninstall.exe"', 'RMDir "$INSTDIR"',
        'Delete "$SMPROGRAMS\\SwiftEdit\\SwiftEdit.lnk"', 'RMDir "$SMPROGRAMS\\SwiftEdit"',
        'DeleteRegKey HKCU "' + uninstall_key + '"', 'SectionEnd'])
    result: str = '\n'.join(lines) + '\n'
    return result


def windows(bundle: Path, output: Path, scratch: Path) -> tuple[Path, str]:
    version: str = project_version()
    installer: Path = output / ('SwiftEdit-' + version + '-windows-x64-setup.exe')
    script: Path = scratch / 'installer.nsi'
    script.write_text(windows_script(bundle, installer, version), encoding='utf-8')
    # Prefer official NSIS: the MSYS2 package has shipped mismatched plugin/stub architectures.
    compiler: str = 'makensis'
    directory: str
    for directory in [os.environ.get('NSIS_HOME', ''), 'C:/Program Files (x86)/NSIS', 'C:/Program Files/NSIS']:
        if directory and (Path(directory) / 'makensis.exe').is_file():
            compiler = str(Path(directory) / 'makensis.exe')
            break
    run([compiler, str(script)])
    installed: Path = scratch / 'installed with spaces'
    invoke_nsis(installer, installed)
    verify_payload(bundle, installed)
    smoke(installed / 'SwiftEdit.exe', installed / 'swiftedit-cli.exe', scratch)
    sentinel: Path = installed / 'user-document.txt'
    sentinel.write_text('preserve this document', encoding='utf-8')
    # Reinstall exercises upgrade-over-existing behavior before uninstall.
    invoke_nsis(installer, installed)
    verify_payload(bundle, installed)
    invoke_nsis(installed / 'Uninstall.exe', installed, True)
    if (installed / 'SwiftEdit.exe').exists() or (installed / 'fonts').exists():
        raise RuntimeError('Uninstaller left application payload behind')
    if sentinel.read_text(encoding='utf-8') != 'preserve this document':
        raise RuntimeError('Installer/uninstaller changed a user document')
    return installer, 'install, payload hashes, GUI/CLI startup, reinstall and document-preserving uninstall passed'


def macos(bundle: Path, output: Path, scratch: Path) -> tuple[Path, str]:
    version: str = project_version()
    installer: Path = output / ('SwiftEdit-' + version + '-macos-arm64.pkg')
    root: Path = scratch / 'pkg-root'
    shutil.copytree(bundle, root / 'SwiftEdit.app', symlinks=True)
    components: Path = scratch / 'components.plist'
    run(['pkgbuild', '--analyze', '--root', str(root), str(components)])
    records: list[dict] = plistlib.loads(components.read_bytes())
    record: dict
    for record in records:
        record['BundleIsRelocatable'] = False
    components.write_bytes(plistlib.dumps(records))
    run(['pkgbuild', '--root', str(root), '--component-plist', str(components),
         '--install-location', '/Applications', '--identifier', 'org.rainstar.swiftedit',
         '--version', version, str(installer)])
    installed: Path = Path('/Applications/SwiftEdit.app')
    if installed.exists():
        raise RuntimeError('Refusing to replace a pre-existing application during CI installation test')
    run(['sudo', '/usr/sbin/installer', '-pkg', str(installer), '-target', '/'])
    verify_payload(bundle, installed)
    run(['codesign', '--verify', '--deep', '--strict', str(installed)])
    smoke(installed / 'Contents/MacOS/SwiftEdit', installed / 'Contents/MacOS/swiftedit-cli', scratch)
    return installer, 'system installation, payload hashes, signature and installed GUI/CLI startup passed'


def linux(bundle: Path, output: Path, scratch: Path) -> tuple[Path, str]:
    version: str = project_version()
    installer: Path = output / ('SwiftEdit-' + version + '-linux-amd64.deb')
    root: Path = scratch / 'deb-root'
    installed: Path = root / 'usr/lib/swiftedit'
    shutil.copytree(bundle, installed)
    command: str
    target: str
    for command, target in [('swiftedit', 'SwiftEdit'), ('swiftedit-terminal', 'swiftedit-terminal'), ('swiftedit-cli', 'swiftedit-cli')]:
        launcher: Path = root / 'usr/bin' / command
        launcher.parent.mkdir(parents=True, exist_ok=True)
        launcher.write_text('#!/bin/sh\nexec /usr/lib/swiftedit/' + target + ' "$@"\n', encoding='utf-8')
        launcher.chmod(0o755)
    desktop: Path = root / 'usr/share/applications/swiftedit.desktop'
    desktop.parent.mkdir(parents=True)
    desktop.write_text('[Desktop Entry]\nType=Application\nName=SwiftEdit\n'
        'Comment=Text editor with Markdown and CSV views\nExec=swiftedit %f\n'
        'Icon=accessories-text-editor\nCategories=Utility;TextEditor;\nTerminal=false\n'
        'MimeType=text/plain;text/markdown;text/csv;\n', encoding='utf-8')
    metadata: Path = scratch / 'debian/control'
    metadata.parent.mkdir()
    metadata.write_text('Source: swiftedit\nSection: editors\nPriority: optional\n'
        'Maintainer: SwiftEdit contributors <noreply@rainstar.invalid>\n\n'
        'Package: swiftedit\nArchitecture: amd64\nDescription: SwiftEdit native text editor\n', encoding='utf-8')
    arguments: list[str] = ['dpkg-shlibdeps', '-O', '-l' + str(installed / 'lib'), '--ignore-missing-info']
    binary: Path
    for binary in list((installed / 'bin').iterdir()) + list((installed / 'lib').iterdir()):
        arguments.append('-e' + str(binary))
    listing: str = run(arguments, scratch)
    dependencies: str = ''
    line: str
    for line in listing.splitlines():
        if line.startswith('shlibs:Depends='):
            dependencies = line.removeprefix('shlibs:Depends=')
    if not dependencies:
        raise RuntimeError('Could not determine Debian runtime dependencies')
    control: Path = root / 'DEBIAN/control'
    control.parent.mkdir()
    control.write_text('Package: swiftedit\nVersion: ' + version + '\nArchitecture: amd64\n'
        'Maintainer: SwiftEdit contributors <noreply@rainstar.invalid>\nSection: editors\n'
        'Priority: optional\nDepends: ' + dependencies + '\n'
        'Description: Native text editor with Markdown and CSV views\n', encoding='utf-8')
    run(['dpkg-deb', '--root-owner-group', '--build', str(root), str(installer)])
    if Path('/usr/lib/swiftedit').exists():
        raise RuntimeError('Refusing to replace a pre-existing application during CI installation test')
    run(['sudo', 'apt-get', 'install', '-y', str(installer)])
    verify_payload(bundle, Path('/usr/lib/swiftedit'))
    smoke(Path('/usr/bin/swiftedit'), Path('/usr/bin/swiftedit-cli'), scratch)
    run(['sudo', 'apt-get', 'remove', '-y', 'swiftedit'])
    if Path('/usr/bin/swiftedit').exists():
        raise RuntimeError('Debian uninstall left launcher behind')
    return installer, 'system installation, payload hashes, installed GUI/CLI startup and package removal passed'


def build_and_verify(bundle: Path, output: Path, platform: str) -> dict[str, str]:
    if os.environ.get('GITHUB_ACTIONS') != 'true':
        raise RuntimeError('Installer installation tests require a dedicated GitHub Actions runner')
    installer: Path
    validation: str
    with tempfile.TemporaryDirectory(prefix='swiftedit-installer-', dir=output.parent) as temporary:
        scratch: Path = Path(temporary)
        if platform == 'windows-x64':
            installer, validation = windows(bundle, output, scratch)
        elif platform == 'macos-arm64':
            installer, validation = macos(bundle, output, scratch)
        elif platform == 'linux-x64':
            installer, validation = linux(bundle, output, scratch)
        else:
            raise ValueError('Unsupported installer platform')
    checksum: str = sha256(installer)
    installer.with_name(installer.name + '.sha256').write_text(
        checksum + '  ' + installer.name + '\n', encoding='utf-8')
    result: dict[str, str] = {'filename': installer.name, 'sha256': checksum,
                              'version': project_version(), 'validation': validation}
    return result
