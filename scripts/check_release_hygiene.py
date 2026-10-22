#!/usr/bin/env python3
"""Reject private work notes and Russian prose in first-party release files."""
import argparse
import ast
from pathlib import Path
import re
import subprocess
import sys
import tokenize
import io

ROOT = Path(__file__).resolve().parents[1]
CYRILLIC = re.compile(r'[\u0400-\u052f]')
WORK_NAME = re.compile(r'(?:^|[_-])(?:plan|roadmap|progress|research|new_chat_prompt|release_validation|draft)(?:[_.-]|$)', re.I)
WORK_SECTION = re.compile(r'(?mi)^(?:#{1,6}\s+(?:stage\s+\d|roadmap|adoption plan|run history|development progress)|(?:Next|Checkpoint):|\s*[-*]\s+\[[ x]\])')
C_TOKENS = re.compile(r'R"(?P<delimiter>[^ ()\\\t\r\n]{0,16})\([\s\S]*?\)(?P=delimiter)"|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|(?P<comment>//[^\n]*|/\*[\s\S]*?\*/)')


def candidates():
    try:
        result = subprocess.run(['git', '-C', str(ROOT), 'ls-files', '-z', '--cached', '--others', '--exclude-standard'],
                                check=True, capture_output=True)
        return sorted(set(filter(None, result.stdout.decode().split('\0'))))
    except (OSError, subprocess.CalledProcessError):
        # Source archives have no .git directory. Check their actual contents.
        return [str(p.relative_to(ROOT)) for p in ROOT.rglob('*') if p.is_file()]


def comments(path, text):
    if path.suffix == '.py':
        try:
            for token in tokenize.generate_tokens(io.StringIO(text).readline):
                if token.type == tokenize.COMMENT:
                    yield token.string
            tree = ast.parse(text)
            for node in ast.walk(tree):
                if isinstance(node, (ast.Module, ast.ClassDef, ast.FunctionDef, ast.AsyncFunctionDef)):
                    doc = ast.get_docstring(node)
                    if doc:
                        yield doc
        except (SyntaxError, tokenize.TokenError):
            return
    elif path.suffix in {'.c', '.cc', '.cpp', '.h', '.hpp', '.mm', '.m'}:
        for match in C_TOKENS.finditer(text):
            if match.group('comment'):
                yield match.group('comment')
    elif path.suffix == '.sh':
        for line in text.splitlines():
            if line.lstrip().startswith('#'):
                yield line


def check_source():
    errors = []
    for name in candidates():
        path = Path(name)
        if not (ROOT / path).is_file():
            continue  # A tracked deletion not yet committed.
        parts = path.parts
        if parts[0] in {'.git', 'build', 'tools', 'ppc', 'game'}:
            continue
        if parts[0] == 'private':
            errors.append(name + ': private work files must not be tracked or archived')
            continue
        if parts[:2] == ('runtime', 'thirdparty') or parts[0] in {'LICENSES', 'assets'}:
            continue  # Preserve upstream data, legal texts and assets verbatim.
        if path.suffix not in {'.md', '.rst', '.txt', '.c', '.cc', '.cpp', '.h', '.hpp', '.mm', '.m', '.py', '.sh'}:
            continue
        text = (ROOT / path).read_text(errors='replace')
        if path.suffix in {'.md', '.rst', '.txt'}:
            if WORK_NAME.search(path.name):
                errors.append(name + ': a work-note filename is not release documentation')
            if CYRILLIC.search(text):
                errors.append(name + ': first-party documentation must be in English')
            if WORK_SECTION.search(text):
                errors.append(name + ': planning/checklist sections belong in private')
        elif CYRILLIC.search(text) and any(CYRILLIC.search(comment) for comment in comments(path, text)):
            errors.append(name + ': first-party comments/docstrings must be in English')
    return errors


def check_app(app):
    errors = []
    for path in app.rglob('*'):
        if not path.is_file():
            continue
        relative = path.relative_to(app)
        if any(part.lower() in {'private', '.git', 'build', 'logs'} for part in relative.parts):
            errors.append(str(relative) + ': private/build content in app')
        if path.suffix.lower() in {'.md', '.rst', '.txt', '.log'} and WORK_NAME.search(path.name):
            errors.append(str(relative) + ': work notes in app')
    return errors


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--app', type=Path, help='also check a packaged .app directory')
    args = parser.parse_args()
    errors = check_source()
    if args.app:
        if not args.app.is_dir():
            errors.append(str(args.app) + ': app directory is missing')
        else:
            errors.extend(check_app(args.app))
    if errors:
        print('\n'.join(errors), file=sys.stderr)
        return 1
    print('Release hygiene: PASS (public files, English documentation/comments, no work notes)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
