#!/usr/bin/env python3
"""Validate the six-language catalogue and explicit presentation keys."""
import argparse
import json
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
STRING = r'"(?:\\.|[^"\\])*"'
STRINGS = rf'(?:{STRING}\s*)+'
TOKENS = re.compile(rf'//[^\n]*|/\*[\s\S]*?\*/|{STRING}|\'(?:\\.|[^\'\\])*\'')
ARGUMENT = re.compile(r'%(?:[-+#0]*[0-9.*]*)(?:hh|ll|[hljztL])?[diuoxXfFeEgGaAcspn%]|\{[^{}]*\}')
BRANDS = {'BurnoutRevenge', 'BURNOUT REVENGE', 'BURNOUT REVENGE  /  XBOX 360', '[BURNOUT REVENGE FOR MAC]'}


def literals(value):
    return ''.join(json.loads(token) for token in re.findall(STRING, value))


def check():
    rows = {}
    errors = []
    path = ROOT / 'runtime/user/localization_catalog.inc'
    for number, line in enumerate(path.read_text().splitlines(), 1):
        if not line.strip() or line.lstrip().startswith('//'):
            continue
        try:
            row = json.loads('[' + line.strip()[2:-3] + ']')
        except (ValueError, IndexError):
            errors.append(f'{path.name}:{number}: invalid catalogue row')
            continue
        if len(row) != 6 or any(not isinstance(s, str) or not s for s in row):
            errors.append(f'{path.name}:{number}: every language needs text')
            continue
        if row[0] in rows:
            errors.append(f'{path.name}:{number}: duplicate English key: {row[0]}')
        rows[row[0]] = row
        signature = ARGUMENT.findall(row[0])
        for language, value in enumerate(row[1:], 2):
            if ARGUMENT.findall(value) != signature:
                errors.append(f'{path.name}:{number}: arguments differ in language {language}: {row[0]}')
            if '###' in row[0] and value.split('###')[-1] != row[0].split('###')[-1]:
                errors.append(f'{path.name}:{number}: translated widget identity')
    patterns = [
        re.compile(rf'locale::(?:Text|Format)\(\s*({STRINGS})'),
        re.compile(rf'(?:TitleText|Legend|(?:burnout|br)::(?:Heading|Eyebrow|DialogText|DialogOption|Button|Link)|\bbutton)\(\s*({STRINGS})'),
        re.compile(rf'ActionRow\(\s*{STRING}\s*,\s*({STRINGS})'),
        re.compile(rf'\bheader\(\s*\d+\s*,\s*({STRINGS})'),
    ]
    checked = set()
    for path in sorted((ROOT / 'runtime').rglob('*.cpp')):
        if 'thirdparty' in path.parts:
            continue
        text = TOKENS.sub(lambda m: m[0] if m[0].startswith('"') else ' ' * len(m[0]), path.read_text())
        for pattern in patterns:
            for match in pattern.finditer(text):
                key = literals(match[1])
                if key in BRANDS or not re.search('[A-Za-z]{2}', key) or key.startswith(('##', 'http')):
                    continue
                checked.add(key)
                if key not in rows:
                    line = text[:match.start()].count('\n') + 1
                    errors.append(f'{path.relative_to(ROOT)}:{line}: missing translation: {key}')
    if errors:
        print('\n'.join(dict.fromkeys(errors)), file=sys.stderr)
        return 1
    print(f'Localization: PASS ({len(rows)} keys, six languages, {len(checked)} explicit presentation keys)')
    return 0


if __name__ == '__main__':
    sys.exit(check())
