class A
{
    int F()
    {
        int x = 1 +
            2;
        var y = items
            .Select(i => i + 1)
            .Where(i => i > 0);
        z = cond
            ? a
            : b;
        return a &&
            b;
    }
}
class B
{
    void G()
    {
        var x = new A
        {
            a = 1,
        };
    }
}
class L
{
    void F()
    {
        Run(i =>
        {
            return i > 0;
        });
        items.Where(i =>
        {
            Inner(b, () =>
            {
                x();
            });
            return i > 0;
        });
        switch (x)
        {
            case 1:
                {
                    y();
                }
                break;
        }
    }
}
