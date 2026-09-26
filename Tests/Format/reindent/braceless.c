void f() {
    if (x)
        foo();
    else if (y)
        bar();
    else
        baz();
    if (a)
        if (b)
            c();
    while (y)
        y--;
    for (;;)
        for (;;)
            z();
    do
        x++;
    while (x < 10);
    if (x) foo();
    if (x)
    {
        foo();
    }
    else
    {
        bar();
    }
    if (x) {
        foo(1,
            2);
    }
}
