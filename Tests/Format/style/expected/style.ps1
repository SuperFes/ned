function Get-Foo {
    if ($x) {
        1
    } elseif ($y) {
        2
    } else {
        3
    }
    try {
    } catch {
    }
}


class Foo {
    [int] Bar() {
        return 1
    }

    [int] Baz() { return 2 }
}
