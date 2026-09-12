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
