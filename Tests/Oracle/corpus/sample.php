<?php

namespace Demo;

interface Sized
{
    public function size(): int;
}

class Widget implements Sized
{
    private array $items = [
        'one',
        'two',
    ];

    public function size(): int
    {
        $total = 0;
        foreach ($this->items as $item) {
            $total += strlen($item);
        }
        return $total;
    }
}

function usage(): string
{
    return <<<EOT
usage: demo [options]
  --verbose        deliberately under-indented
        --quiet    and deliberately over-indented
EOT;
}
