# Director checks. None of them is part of the default build: ask for them
# with "ninja director_tests", then run them with run-tests.sh.
#
# Included from qmlui/CMakeLists.txt, so ${module_name} is the application.

set(app ${module_name})
set(director_dir ${CMAKE_CURRENT_SOURCE_DIR}/..)
set(app_includes $<TARGET_PROPERTY:${app},INCLUDE_DIRECTORIES>)
set(qt Qt${QT_MAJOR_VERSION})

# The decision engine alone: no QLC+ dependencies
add_executable(directorengine_test EXCLUDE_FROM_ALL
    directorengine_test.cpp
    ${director_dir}/directorengine.cpp
)
target_include_directories(directorengine_test PRIVATE ${director_dir})
target_link_libraries(directorengine_test PRIVATE ${qt}::Core ${qt}::Test)

# The manager against a real Doc
add_executable(directormanager_test EXCLUDE_FROM_ALL
    directormanager_test.cpp
    ${director_dir}/directormanager.cpp
    ${director_dir}/directorengine.cpp
)
target_include_directories(directormanager_test PRIVATE ${app_includes})
target_link_libraries(directormanager_test PRIVATE
    ${qt}::Core ${qt}::Gui ${qt}::Widgets ${qt}::Multimedia ${qt}::Network
    ${qt}::Qml ${qt}::Quick ${qt}::Test
    qlcplusengine
)

# The panel on its own, to catch QML warnings and take a screenshot
add_executable(directorpanel_preview EXCLUDE_FROM_ALL
    directorpanel_preview.cpp
    ${director_dir}/directormanager.cpp
    ${director_dir}/directorengine.cpp
)
target_include_directories(directorpanel_preview PRIVATE ${app_includes})
target_link_libraries(directorpanel_preview PRIVATE
    ${qt}::Core ${qt}::Gui ${qt}::Widgets ${qt}::Multimedia ${qt}::Network
    ${qt}::Qml ${qt}::Quick
    qlcplusengine
)

# End to end: the whole application, with director_e2e.cpp in place of
# main.cpp. Reuses the objects of the application instead of compiling its
# sources a second time.
add_executable(director_e2e EXCLUDE_FROM_ALL
    director_e2e.cpp
    $<FILTER:$<TARGET_OBJECTS:${app}>,EXCLUDE,[/\\]main\.cpp\.>
)
add_dependencies(director_e2e ${app})
# Same kind of executable as the application it stands in for: on Windows
# its objects are built for Qt's entry point, which only a GUI executable gets
get_target_property(app_win32 ${app} WIN32_EXECUTABLE)
if(app_win32)
    set_target_properties(director_e2e PROPERTIES WIN32_EXECUTABLE ON)
endif()
target_include_directories(director_e2e PRIVATE ${app_includes})
target_compile_definitions(director_e2e PRIVATE $<TARGET_PROPERTY:${app},COMPILE_DEFINITIONS>)
# Read now rather than through a generator expression: Qt decides what else
# an executable needs (its entry point on Windows) from a plain library list
get_target_property(app_libraries ${app} LINK_LIBRARIES)
target_link_libraries(director_e2e PRIVATE ${app_libraries})

add_custom_target(director_tests DEPENDS
    directorengine_test directormanager_test directorpanel_preview director_e2e
)
