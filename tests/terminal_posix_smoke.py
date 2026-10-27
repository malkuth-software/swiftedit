"""Exercise only a spawned SwiftEdit child on an owned disposable pseudo-terminal."""
from __future__ import annotations
import argparse
import fcntl
import os
from pathlib import Path
import pty
import select
import signal
import struct
import subprocess
import tempfile
import termios
import time


def terminal_mode(slave: int) -> list[object]:
    # Darwin sets transient PENDIN when restoring ICANON. FIONREAD processes
    # pending input through the restored discipline without consuming/flushing it.
    # Compare every mode field after that kernel transition, without masking bits.
    # See apple-oss-distributions/xnu bsd/kern/tty.c: ttioctl and ttnread.
    fcntl.ioctl(slave, termios.FIONREAD, struct.pack('i', 0))
    mode: list[object] = termios.tcgetattr(slave)
    return mode


def await_bytes(master: int, child: subprocess.Popen[bytes], marker: bytes) -> None:
    deadline: float = time.monotonic() + 8.0
    observed: bytes = b''
    while time.monotonic() < deadline:
        if child.poll() is not None:
            raise RuntimeError('Terminal child exited before readiness')
        readable: list[int]
        readable, _, _ = select.select([master], [], [], 0.1)
        if readable:
            observed += os.read(master, 65536)
            if marker in observed:
                return
            observed = observed[-65536:]
    raise RuntimeError('Terminal readiness timed out')


def finish(master: int, child: subprocess.Popen[bytes]) -> int:
    deadline: float = time.monotonic() + 8.0
    while time.monotonic() < deadline:
        result: int | None = child.poll()
        if result is not None:
            return result
        readable: list[int]
        readable, _, _ = select.select([master], [], [], 0.05)
        if readable:
            os.read(master, 65536)
    raise RuntimeError('Terminal child did not finish')


def exercise(executable: Path, path: Path, interrupt: int | None,
             partial_input: bytes = b'') -> None:
    master: int
    slave: int
    master, slave = pty.openpty()
    child: subprocess.Popen[bytes] | None = None
    try:
        fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack('HHHH', 24, 80, 0, 0))
        original: list[object] = terminal_mode(slave)
        child = subprocess.Popen([str(executable), str(path)], stdin=slave, stdout=slave,
                                 stderr=slave, start_new_session=True)
        await_bytes(master, child, b'\x1b[?2004h')
        if termios.tcgetattr(slave) == original:
            raise RuntimeError('Terminal did not enter raw input mode')
        if interrupt is not None:
            if partial_input and os.write(master, partial_input) != len(partial_input):
                raise RuntimeError('Incomplete partial input write')
            fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack('HHHH', 25, 81, 0, 0))
            child.send_signal(signal.SIGWINCH)
            child.send_signal(interrupt)
        else:
            payload: bytes = 'é😀\r\n'.encode('utf-8') + b'\x13\x18'
            keys: bytes = b'\x1b[200~' + payload + b'\x1b[201~\x1a\x19\x13\x18'
            written: int = os.write(master, keys)
            if written != len(keys):
                raise RuntimeError('Incomplete smoke input write')
        result: int = finish(master, child)
        if (interrupt is not None and result == 0) or (interrupt is None and result != 0):
            raise RuntimeError('Unexpected terminal exit status: ' + str(result))
        restored: list[object] = terminal_mode(slave)
        if restored != original:
            print('Terminal original mode:', repr(original), flush=True)
            print('Terminal restored mode:', repr(restored), flush=True)
            print('Interrupted case:', interrupt, flush=True)
            raise RuntimeError('Terminal mode was not restored')
        expected: bytes = b'base\n'
        if interrupt is None:
            expected = 'é😀\r\n'.encode('utf-8') + b'\x13\x18base\n'
        if path.read_bytes() != expected:
            raise RuntimeError('Paste, undo/redo, save or interruption changed incorrect bytes')
    finally:
        if child is not None and child.poll() is None:
            child.kill()
            child.wait()
        os.close(master)
        os.close(slave)



def exercise_publication_signal(executable: Path, path: Path) -> None:
    master: int
    slave: int
    master, slave = pty.openpty()
    child: subprocess.Popen[bytes] | None = None
    try:
        fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack('HHHH', 24, 80, 0, 0))
        original: list[object] = terminal_mode(slave)
        child = subprocess.Popen([str(executable), str(path)], stdin=slave, stdout=slave,
                                 stderr=slave, start_new_session=True)
        await_bytes(master, child, b'\x1b[?2004h')
        if os.write(master, b'\x14\r') != 2:
            raise RuntimeError('Incomplete copy command write')
        await_bytes(master, child, b'Publishing text copy...')
        child.send_signal(signal.SIGTERM)
        result: int = finish(master, child)
        if result == 0 or terminal_mode(slave) != original:
            raise RuntimeError('Publication interruption did not restore the terminal')
        if path.stat().st_size != 16777216:
            raise RuntimeError('Publication interruption changed the source size')
    finally:
        if child is not None and child.poll() is None:
            child.kill()
            child.wait()
        os.close(master)
        os.close(slave)



def exercise_partial_copy(executable: Path, path: Path, partial: bytes, remainder: bytes) -> None:
    master: int
    slave: int
    master, slave = pty.openpty()
    child: subprocess.Popen[bytes] | None = None
    try:
        fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack('HHHH', 24, 80, 0, 0))
        original: list[object] = terminal_mode(slave)
        child = subprocess.Popen([str(executable), str(path)], stdin=slave, stdout=slave,
                                 stderr=slave, start_new_session=True)
        await_bytes(master, child, b'\x1b[?2004h')
        keys: bytes = b'\x14\r' + partial
        if os.write(master, keys) != len(keys):
            raise RuntimeError('Incomplete partial-copy input write')
        # No more input is supplied until the copy reports completion. A raw-byte
        # readiness implementation blocks inside read and times out here.
        await_bytes(master, child, b'Text copy saved; open document unchanged')
        ending: bytes = remainder + b'\x18'
        if os.write(master, ending) != len(ending):
            raise RuntimeError('Incomplete partial-copy finishing input write')
        if finish(master, child) != 0 or terminal_mode(slave) != original:
            raise RuntimeError('Partial-copy test did not exit and restore the terminal')
        target: Path = path.with_name(path.stem + '.1' + path.suffix)
        if target.read_bytes() != path.read_bytes():
            raise RuntimeError('Partial terminal input altered the text copy')
    finally:
        if child is not None and child.poll() is None:
            child.kill()
            child.wait()
        os.close(master)
        os.close(slave)


def main() -> None:
    parser: argparse.ArgumentParser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--executable', type=Path, required=True)
    arguments: argparse.Namespace = parser.parse_args()
    executable: Path = arguments.executable.resolve(strict=True)
    with tempfile.TemporaryDirectory(prefix='swiftedit-terminal-') as directory:
        path: Path = Path(directory) / 'café.txt'
        path.write_bytes(b'base\n')
        exercise(executable, path, None)
        interruption: int
        partial: bytes
        for interruption in [signal.SIGINT, signal.SIGTERM, signal.SIGHUP, signal.SIGTSTP]:
            for partial in [b'', b'\x1b', b'\xf0', b'\x1b[200~unfinished']:
                path.write_bytes(b'base\n')
                exercise(executable, path, interruption, partial)
        publication_source: Path = Path(directory) / 'publication-source.txt'
        with publication_source.open('wb') as source:
            source.truncate(16777216)
        exercise_publication_signal(executable, publication_source)
        partial_cases: list[tuple[bytes, bytes]] = [
            (b'\xf0', b'\x9f\x98\x80'),
            (b'\x1b[200~unfinished', b'\x1b[201~')]
        index: int
        for index, (partial, remainder) in enumerate(partial_cases):
            copy_source: Path = Path(directory) / ('partial-copy-' + str(index) + '.txt')
            with copy_source.open('wb') as source:
                source.truncate(16777216)
            exercise_partial_copy(executable, copy_source, partial, remainder)
    print('POSIX terminal: atomic Unicode/control paste, undo/redo, save, exit, resize and '
          'mode restoration across 16 signal cases, publication interruption and partial-input copies passed.')


if __name__ == '__main__':
    main()
