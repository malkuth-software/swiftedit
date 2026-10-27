"""Regenerate the checked-in Unicode 17 character table from pinned data."""
from pathlib import Path
import hashlib

root: Path = Path(__file__).resolve().parents[1]
data_dir: Path = root / "third_party" / "unicode"
expected: dict[str, str] = {
    "UnicodeData.txt": "2e1efc1dcb59c575eedf5ccae60f95229f706ee6d031835247d843c11d96470c",
    "NameAliases.txt": "793f6f1e4d15fd90f05ae66460191dc4d75d1fea90136a25f30dd6a4cb950eac",
}
for name, digest in expected.items():
    actual: str = hashlib.sha256((data_dir / name).read_bytes()).hexdigest()
    if actual != digest:
        raise RuntimeError("Unexpected Unicode input: " + name)

aliases: dict[int, str] = {}
for line in (data_dir / "NameAliases.txt").read_text(encoding="utf-8").splitlines():
    content: str = line.split("#", 1)[0].strip()
    if not content:
        continue
    fields: list[str] = content.split(";")
    scalar: int = int(fields[0], 16)
    if fields[2] == "control" and scalar not in aliases:
        aliases[scalar] = fields[1]

rows: list[str] = ["// Generated from Unicode 17.0.0 by tools/Generate-Unicode.py.",
                   "// Unicode License V3: third_party/unicode/LICENSE.txt."]
first: int = 0
range_name: str = ""
previous: int = -1
for line in (data_dir / "UnicodeData.txt").read_text(encoding="utf-8").splitlines():
    fields = line.split(";")
    scalar = int(fields[0], 16)
    name = fields[1]
    category: str = fields[2]
    if name.endswith(", First>"):
        first = scalar
        range_name = name[1:-8]
        continue
    end: int = scalar
    if name.endswith(", Last>"):
        scalar = first
        name = range_name
    elif name == "<control>":
        name = aliases.get(scalar, "CONTROL")
    if scalar <= previous:
        raise RuntimeError("Unordered Unicode data")
    previous = end
    escaped: str = name.replace("\\", "\\\\").replace('"', '\\"')
    rows.append(f'{{0x{scalar:X}, 0x{end:X}, "{category}", "{escaped}"}},')

output: Path = root / "src" / "unicode_data.inc"
output.write_text("\n".join(rows) + "\n", encoding="utf-8")
print(f"Generated {len(rows) - 2} Unicode records")
