#!/usr/bin/env python3
"""Checks the documents of the package.

The check has two parts. The first one finds a name of a work item that
belongs to the pipeline. The package must stand alone, so it may not name
one. The second one checks the sentences and the paragraphs of the
markdown files. A sentence holds at most 20 words. A paragraph holds at
most 6 sentences.

The script lives in the package, so a push that touches only the
documents runs it without the whole test matrix.

Usage: python3 test/docs_check.py
Exit 0 = clean. Exit 1 = the documents hold a finding.
"""
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent.parent
SELF = Path(__file__).resolve()

MAX_WORDS = 20
MAX_SENTENCES = 6

# The names that point at the pipeline. The parts of the names are joined
# at run time, so this file does not hold a name itself.
ARTIFACT = "|".join([
    "spec" + "s/",
    "spec" + "kit",
    r"\bspec(s)? [0-9]{3}\b",
    r"\b[0-9]{3}-[a-z]+(-[a-z0-9]+)+\b.*spec",
    r"\b(FR|SC|US)-?[0-9]+\b",
    r"\bT0[0-9]{2}[a-z]?\b",
    ".stage-" + "ledger",
    "PROPOS" + "AL.md",
    "CLAU" + "DE.md",
])

SUFFIXES = (".goose", ".md", ".c", ".h", ".yml", ".yaml", ".py", ".toml")
SENTENCE_END = re.compile(r"[.!?](?=\s|$)")
WORD = re.compile(r"\S+")


def strip_md_noise(text):
    """Drop code fences, headings markup, table rows and list markers."""
    out = []
    in_fence = False
    for line in text.splitlines():
        if line.strip().startswith("```"):
            in_fence = not in_fence
            continue
        if in_fence:
            continue
        line = re.sub(r"^#{1,6}\s+", "", line)
        if re.match(r"^\s*\|", line):
            continue
        if re.match(r"^([-*+]\s+|\d+\.\s+)", line):
            out.append("\n\n")
            line = re.sub(r"^[-*+]\s+", "", line)
            line = re.sub(r"^\d+\.\s+", "", line)
        line = re.sub(r"`[^`]*`", "code", line)
        out.append(line)
    return "\n".join(out)


def check_artifacts():
    """Finds a name of a work item in a file of the package."""
    findings = []
    for path in sorted(HERE.rglob("*")):
        if not path.is_file() or path.suffix not in SUFFIXES:
            continue
        if ".git" in path.parts or path.resolve() == SELF:
            continue
        for number, line in enumerate(
                path.read_text(errors="replace").splitlines(), 1):
            if re.search(ARTIFACT, line):
                findings.append("%s:%d: %s"
                                % (path.relative_to(HERE), number,
                                   line.strip()[:80]))
    return findings


def check_sentences():
    """Checks the sentences and the paragraphs of the markdown files."""
    findings = []
    for path in sorted(HERE.glob("*.md")):
        text = strip_md_noise(path.read_text(errors="replace"))
        for paragraph in re.split(r"\n\s*\n", text):
            if not paragraph.strip():
                continue
            sentences = [s for s in SENTENCE_END.split(paragraph)
                         if s.strip()]
            if len(sentences) > MAX_SENTENCES:
                findings.append("%s: a paragraph holds %d sentences"
                                % (path.name, len(sentences)))
            for sentence in sentences:
                words = len(WORD.findall(sentence))
                if words > MAX_WORDS:
                    findings.append("%s: a sentence holds %d words: %s"
                                    % (path.name, words,
                                       sentence.strip()[:60]))
    return findings


def main():
    findings = check_artifacts() + check_sentences()
    for finding in findings:
        print(finding)
    if findings:
        print("docs check FAILED with %d finding(s)" % len(findings))
        return 1
    print("docs ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
