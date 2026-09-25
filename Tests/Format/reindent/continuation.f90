program p
    integer :: x, &
        y
    x = 1 + &
        2 + &
        3
    call foo(a, &
        b)
    print *, 'a', &
        'b'
    if (x > 1 .and. &
        y > 2) then
        x = 0
    end if
end program p
