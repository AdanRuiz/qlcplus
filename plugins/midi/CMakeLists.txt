project(midi)

add_subdirectory(src)
if(tests AND NOT ANDROID AND NOT IOS)
    add_subdirectory(test)
endif()
