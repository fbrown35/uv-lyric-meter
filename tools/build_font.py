"""Install the unmodified Pretendard Regular TTF from its official release ZIP.
Usage: python tools/build_font.py path/to/Pretendard-1.3.9.zip
Requires fonttools for validation; does not subset or rename the font.
"""
import argparse
from pathlib import Path
import zipfile
from io import BytesIO
from fontTools.ttLib import TTFont

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("release_zip", type=Path)
args = parser.parse_args()
with zipfile.ZipFile(args.release_zip) as archive:
    data = archive.read("public/static/alternative/Pretendard-Regular.ttf")
    license_text = archive.read("LICENSE.txt")
font = TTFont(BytesIO(data))
assert "glyf" in font and "fvar" not in font, "A static TrueType font is required"
assert font["OS/2"].usWeightClass == 400, "Expected Regular weight"
cmap = font.getBestCmap()
assert all(code in cmap for code in range(0xAC00, 0xD7A4)), "Missing Hangul syllables"
assert all(ord(char) in cmap for char in "가나다한글ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"), "Missing UI glyphs"
font.close()
output = Path(__file__).resolve().parents[1] / "assets/fonts"
output.mkdir(parents=True, exist_ok=True)
(output / "Pretendard-Regular.ttf").write_bytes(data)
(output / "OFL-Pretendard.txt").write_bytes(license_text)
print(f"Installed unmodified Pretendard Regular: {len(data)} bytes, {len(cmap)} mapped characters")
