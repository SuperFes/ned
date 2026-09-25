import os


@dataclass
class Shape(Base):
    def area(self, kind):
        if kind is None:
            return [
                1,
                2,
            ]
        elif kind == "square":
            pass
        else:
            try:
                check(kind)
            except ValueError as e:
                raise
            finally:
                done()
        with open(path) as fh:
            for line in fh:
                yield line
        match kind:
            # the common case first
            case 1:
                return 1
            case [first, *rest] if first:
                return first
            case _:
                return 2


def countdown(
    start,
    step,
):
    while start > 0:
        start -= step
    return start
