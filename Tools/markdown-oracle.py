#!/usr/bin/env python3
"""Check that ned's Markdown reindent never changes what a document says.

    Tools/markdown-oracle.py [--ned PATH] [FILE.md ...]

Reindents every example of the GFM spec (Source/Languages/markdown/corpus/
spec.txt) and any FILE given, with `ned --format`, and compares each one's
rendering before and after with a reference renderer: `cmark` (CommonMark's
own) for spec examples, `pandoc -f gfm` for files, since real documents carry
GFM tables that cmark reads as paragraphs. A second `ned --format` pass must
change nothing.

Leading whitespace is most of Markdown's structure -- four columns make code,
a fence's indent is stripped from its content, a list item owns exactly the
lines at its content column -- so a reindent is only correct when rendering
agrees. Tests/MarkdownReindentSpecTest.cpp pins the reindented spec examples;
rerun this before reblessing it.

`markdown-it` (the Python port) is deliberately not used: it hangs on some
spec examples.
"""

import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SPEC = os.path.join(REPO, 'Source', 'Languages', 'markdown', 'corpus', 'spec.txt')


def spec_examples():
    # Corpus separators are 80 characters wide; examples contain `---`/`===`.
    text = open(SPEC, encoding='utf-8').read()
    pattern = r'^={20,}\n(Example \d+)[^\n]*\n(?::[^\n]*\n)*={20,}\n(.*?)\n^-{20,}\s*$'
    return {m.group(1): m.group(2) + '\n' for m in re.finditer(pattern, text, re.M | re.S)}


def render(text, gfm):
    command = ['pandoc', '-f', 'gfm', '-t', 'html'] if gfm else ['cmark']
    return subprocess.run(command, input=text, capture_output=True, text=True, timeout=60, check=True).stdout


def reindent(ned, paths):
    for i in range(0, len(paths), 200):
        subprocess.run([ned, '--format'] + paths[i:i + 200], capture_output=True, check=False)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--ned', default=os.path.join(REPO, 'build', 'Source', 'ned'))
    parser.add_argument('files', nargs='*')
    args = parser.parse_args()
    for tool in ('cmark', 'pandoc'):
        if shutil.which(tool) is None:
            sys.exit(f'markdown-oracle: {tool} is not installed')

    cases = {name: (text, False) for name, text in spec_examples().items()}
    for path in args.files:
        cases[path] = (open(path, encoding='utf-8').read(), True)

    with tempfile.TemporaryDirectory() as work:
        paths = {}
        for index, name in enumerate(cases):
            paths[name] = os.path.join(work, f'{index}.md')
            with open(paths[name], 'w', encoding='utf-8') as out:
                out.write(cases[name][0])
        reindent(args.ned, list(paths.values()))
        once = {name: open(path, encoding='utf-8').read() for name, path in paths.items()}
        reindent(args.ned, list(paths.values()))
        twice = {name: open(path, encoding='utf-8').read() for name, path in paths.items()}

    failures = 0
    for name, (original, gfm) in cases.items():
        if once[name] != original and render(original, gfm) != render(once[name], gfm):
            print(f'meaning changed: {name}')
            failures += 1
        if twice[name] != once[name]:
            print(f'not idempotent: {name}')
            failures += 1
    print(f'{len(cases)} documents, {failures} failures')
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
