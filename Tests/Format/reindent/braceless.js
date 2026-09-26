function f() {
  if (x)
    foo();
  else if (y)
    bar();
  else
    baz();
  for (const a of xs)
    if (a)
      use(a);
  do
    i++;
  while (i < 10);
}
