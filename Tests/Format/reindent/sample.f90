module shapes
    implicit none
    type :: point
        real :: x
    end type point
    interface area
        module procedure area_r
    end interface area
contains
    function area_r(w) result(a)
        real, intent(in) :: w
        real :: a
        select case (int(w))
        case (0)
            a = 0.0
        case default
            a = w
        end select
    end function area_r
end module shapes
program p
    use shapes
    integer :: i
    do i = 1, 3
        if (i > 1) then
            print *, i
        else if (i == 1) then
            print *, 1
        else
            print *, 0
        end if
    end do
    do while (i > 0)
        i = i - 1
    end do
    block
        integer :: j
    end block
    associate (y => i)
        print *, y
    end associate
    where (arr > 0)
        arr = 1
    elsewhere
        arr = 0
    end where
contains
    subroutine s()
        print *, 1
    end subroutine s
end program p
