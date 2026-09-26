<?php
function f()
{
    if ($x)
        foo();
    elseif ($y)
        bar();
    else
        baz();
    foreach ($xs as $a)
        use_it($a);
    for ($i = 0; $i < 3; $i++):
        echo $i;
    endfor;
}
