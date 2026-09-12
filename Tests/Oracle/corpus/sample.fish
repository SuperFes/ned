function greet --argument-names name
    if test -n "$name"
        echo "hi $name"
    else
        echo hi
    end
    for f in *.txt
        set -l words (string split ' ' (cat $f) \
            | string match -r '\S+')
        echo $words
    end
end
