if(WIN32)
  set(A 1)
elseif(APPLE)
  set(A 3)
else()
  set(A 2)
endif()

function(add_thing name)
  foreach(x IN LISTS ARGN)
    message(${x})
  endforeach()
  set(SOURCES
    a.cpp
    b.cpp
  )
endfunction()

while(COND)
  break()
endwhile()
