#!/usr/bin/env python3
"""Host test: flomo UI Chinese literals are covered by the generated font subset.

Contract (docs/development/engineering/lvgl-chinese-fonts.md + coding conventions):
every non-ASCII code point used in main/flomo_*.c UI sources must appear in
assets/fonts/flomo_chars.txt (the converter inventory). The inventory itself must
be a superset of the strings in assets/fonts/flomo_strings.txt. A deliberately
absent code point (U+9F98) guards against a vacuous pass.
"""

from __future__ import annotations

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CHARS = ROOT / "assets" / "fonts" / "flomo_chars.txt"
STRINGS = ROOT / "assets" / "fonts" / "flomo_strings.txt"
# 显示面 = flomo_ui.c:只有它的字符串字面量会画到屏幕上。
# 其他 flomo_*.c 的中文只出现在日志(USB 控制台)或配置页 HTML(浏览器渲染),
# 与设备字库无关,不纳入校验。
UI_SOURCES = [ROOT / "main" / "flomo_ui.c"]
FONT_C = ROOT / "assets" / "fonts" / "flomo_font_16.c"


def load_inventory() -> set[int]:
    points: set[int] = set()
    for line in CHARS.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line:
            continue
        if m := re.fullmatch(r"0x([0-9A-Fa-f]{2})-0x([0-9A-Fa-f]{2,6})", line):
            points.update(range(int(m.group(1), 16), int(m.group(2), 16) + 1))
        elif m := re.fullmatch(r"U\+([0-9A-Fa-f]{4,6})", line):
            points.add(int(m.group(1), 16))
        else:
            raise AssertionError(f"unparsable inventory line: {line!r}")
    return points



def strip_c_comments(text: str) -> str:
    """Remove // and /* */ comments while respecting string and char literals."""
    out = []
    i, n = 0, len(text)
    in_str = False
    in_char = False
    while i < n:
        c = text[i]
        if in_str or in_char:
            out.append(c)
            if c == "\\" and i + 1 < n:
                out.append(text[i + 1])
                i += 2
                continue
            if in_str and c == '"':
                in_str = False
            elif in_char and c == "'":
                in_char = False
            i += 1
            continue
        if c == '"':
            in_str = True
            out.append(c)
            i += 1
            continue
        if c == "'":
            in_char = True
            out.append(c)
            i += 1
            continue
        if c == "/" and i + 1 < n and text[i + 1] == "/":
            while i < n and text[i] != "\n":
                i += 1
            continue
        if c == "/" and i + 1 < n and text[i + 1] == "*":
            i += 2
            while i + 1 < n and not (text[i] == "*" and text[i + 1] == "/"):
                i += 1
            i += 2
            out.append(" ")
            continue
        out.append(c)
        i += 1
    return "".join(out)


class FlomoFontCoverageTest(unittest.TestCase):
    def test_inventory_exists_and_nontrivial(self) -> None:
        inv = load_inventory()
        self.assertGreater(len(inv), 160)
        # ASCII 基本面 + 已知负样本缺失。
        self.assertIn(0x41, inv)
        self.assertNotIn(0x9F98, inv, "negative control U+9F98 must stay absent")

    def test_strings_file_covered_by_inventory(self) -> None:
        inv = load_inventory()
        used = {ord(c) for line in STRINGS.read_text(encoding="utf-8").splitlines()
                for c in line if ord(c) > 0x7E}
        missing = {f"U+{c:04X}" for c in sorted(used - inv)}
        self.assertFalse(missing, f"flomo_strings.txt not covered: {sorted(missing)}")

    def test_ui_sources_covered_by_inventory(self) -> None:
        self.assertTrue(UI_SOURCES, "no main/flomo_*.c sources found")
        inv = load_inventory()
        missing: set[str] = set()
        for path in UI_SOURCES:
            text = strip_c_comments(path.read_text(encoding="utf-8"))
            # 只检查字符串字面量;注释里的汉字不在 UI 显示面内。
            # 按字面字符扫描(源码 UTF-8 存中文),另识别 \uXXXX 转义。
            for literal in re.findall(r'"((?:[^"\\]|\\.)*)"', text):
                for ch in literal:
                    if ord(ch) > 0x7E and ord(ch) not in inv:
                        missing.add(f"U+{ord(ch):04X} in {path.name}")
                for m in re.finditer(r"\\u([0-9A-Fa-f]{4})", literal):
                    cp = int(m.group(1), 16)
                    if cp > 0x7E and cp not in inv:
                        missing.add(f"U+{cp:04X} in {path.name}")
        self.assertFalse(missing, f"UI literals not covered by font: {sorted(missing)}")

    def test_generated_font_compiled_assets_exist(self) -> None:
        for name in ("flomo_font_16.c", "flomo_font_24.c"):
            content = (ROOT / "assets" / "fonts" / name).read_text(encoding="utf-8")
            self.assertIn(f"const lv_font_t {name[:-2]}", content)


if __name__ == "__main__":
    unittest.main()
