project(hotplugmonitor)

if(NOT ANDROID AND NOT IOS)
    add_subdirectory(src)
    if(tests)
        add_subdirectory(test)
    endif()
endif()
