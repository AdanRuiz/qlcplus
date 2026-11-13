project(webaccess)

add_subdirectory(src)
add_subdirectory(res)
if(tests AND NOT ANDROID AND NOT IOS)
    add_subdirectory(test)
endif()
