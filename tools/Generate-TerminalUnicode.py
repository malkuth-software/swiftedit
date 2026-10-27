"""Generate pinned Unicode 17 terminal classification ranges."""
from pathlib import Path
import hashlib

root: Path = Path(__file__).resolve().parents[1]
data_dir: Path = root / "third_party" / "unicode"
expected: dict[str, str] = {
    "UnicodeData.txt": "2e1efc1dcb59c575eedf5ccae60f95229f706ee6d031835247d843c11d96470c",
    "EastAsianWidth.txt": "ea7ce50f3444a050333448dffef1cadd9325af55cbb764b4a2280faf52170a33",
    "emoji-data.txt": "2cb2bb9455cda83e8481541ecf5b6dfda66a3bb89efa3fa7c5297eccf607b72b",
}
for name, digest in expected.items():
    actual: str = hashlib.sha256((data_dir / name).read_bytes()).hexdigest()
    if actual != digest:
        raise RuntimeError("Unexpected Unicode input: " + name)

flags: bytearray = bytearray(0x110000)
first: int = 0
for line in (data_dir / "UnicodeData.txt").read_text(encoding="utf-8").splitlines():
    fields: list[str] = line.split(";")
    scalar: int = int(fields[0], 16)
    name: str = fields[1]
    category: str = fields[2]
    if name.endswith(", First>"):
        first = scalar
        continue
    begin: int = first if name.endswith(", Last>") else scalar
    value: int = 1 if category in ("Cc", "Cf", "Zl", "Zp") else 0
    if category in ("Mn", "Me"):
        value |= 2
    for index in range(begin, scalar + 1):
        flags[index] |= value

for filename, property_name, bit in (("EastAsianWidth.txt", "wide", 4),
                                     ("emoji-data.txt", "Emoji_Presentation", 8)):
    for line in (data_dir / filename).read_text(encoding="utf-8").splitlines():
        content: str = line.split("#", 1)[0].strip()
        if not content:
            continue
        fields = content.split(";")
        prop: str = fields[1].strip()
        selected: bool = prop in ("W", "F") if property_name == "wide" else prop == property_name
        if not selected:
            continue
        bounds: list[str] = fields[0].strip().split("..")
        begin = int(bounds[0], 16)
        end: int = int(bounds[-1], 16)
        for index in range(begin, end + 1):
            flags[index] |= bit

rows: list[str] = ["// Generated Unicode 17 terminal properties. See tools/Generate-TerminalUnicode.py.",
                   "// Unicode License V3: third_party/unicode/LICENSE.txt."]
start: int = 0
for index in range(1, len(flags) + 1):
    if index < len(flags) and flags[index] == flags[start]:
        continue
    if flags[start]:
        rows.append(f"    {{0x{start:X}, 0x{index - 1:X}, {flags[start]}}},")
    start = index
(root / "src" / "terminal_unicode.inc").write_text("\n".join(rows) + "\n", encoding="utf-8")
print("Generated terminal classification ranges:", len(rows) - 2)
