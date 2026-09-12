import os
from typing import List


class Widget:
    """Indentation-based blocks, not braces."""

    def __init__(self, name: str) -> None:
        self.name = name

    def shout(self) -> str:
        return self.name.upper()


def total(values: List[int]) -> int:
    result = 0
    for value in values:
        result += value
    return result


def usage() -> str:
    """A multi-line docstring whose interior is the value.

  This line is deliberately under-indented and
        this one over-indented: a reindent that touches either
    has edited what the program says.
    """
    return usage.__doc__


def commented():
    # A comment as the body's first line: the header is still the def.
    return 1
