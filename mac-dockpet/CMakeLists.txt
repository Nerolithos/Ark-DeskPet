cmake_minimum_required(VERSION 3.15)
project(RosMonDockPet LANGUAGES C CXX OBJCXX)

if(NOT APPLE)
  message(FATAL_ERROR "This target is intended to be built on macOS (APPLE).")
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(SFML REQUIRED sfml-graphics sfml-window sfml-system)

# Build and link spine-cpp from the repo so we can use the spine headers and runtime
set(SPINE_RUNTIMES_DIR "${CMAKE_SOURCE_DIR}/../spine-runtimes/spine-cpp")
set(SPINE_CPP_BIN_DIR "${CMAKE_BINARY_DIR}/spine-cpp-build")

add_subdirectory(${SPINE_RUNTIMES_DIR} ${SPINE_CPP_BIN_DIR})
# spine-cpp disables RTTI/exceptions by default; ensure RTTI and exceptions are enabled for linking with our C++ target
if(TARGET spine-cpp)
  message(STATUS "Enabling RTTI and exceptions for spine-cpp target to work with C++ RTTI in this project")
  target_compile_options(spine-cpp PUBLIC "-frtti" "-fexceptions")
endif()

set(SOURCES
  runros_sfml_dockpet.cpp
  MacWindowUtils.mm
)

add_executable(RosMonDockPet ${SOURCES})

target_include_directories(RosMonDockPet PRIVATE ${SFML_INCLUDE_DIRS} ${CMAKE_SOURCE_DIR}/.. ${CMAKE_SOURCE_DIR}/../spine-runtimes/spine-cpp/spine-cpp/include)
target_compile_features(RosMonDockPet PRIVATE cxx_std_17)

target_link_libraries(RosMonDockPet PRIVATE ${SFML_LIBRARIES} "-framework Cocoa" "-framework OpenGL")

# Link spine-cpp runtime
target_link_libraries(RosMonDockPet PRIVATE spine-cpp)

# Silence macOS OpenGL deprecation warnings (optional) and ensure availability
target_compile_definitions(RosMonDockPet PRIVATE GL_SILENCE_DEPRECATION)

if(SFML_LDFLAGS)
  target_link_options(RosMonDockPet PRIVATE ${SFML_LDFLAGS})
endif()

set_target_properties(RosMonDockPet PROPERTIES
  MACOSX_BUNDLE TRUE
  XCODE_ATTRIBUTE_CLANG_CXX_LIBRARY "libc++"
)

# Copy runtime resources (atlas, skel, textures) into the app bundle Resources so the .app can run by double-click
file(GLOB ROS_PNGS
  "${CMAKE_SOURCE_DIR}/../*rosmon*.png"
  "${CMAKE_SOURCE_DIR}/../build_char_*rosmon*.png"
)
# Also include a background.png next to the other resources if present
if(EXISTS "${CMAKE_SOURCE_DIR}/../background.png")
  list(APPEND ROS_PNGS "${CMAKE_SOURCE_DIR}/../background.png")
endif()
set(ROS_FILES "${CMAKE_SOURCE_DIR}/../rosmon.atlas" "${CMAKE_SOURCE_DIR}/../rosmon.skel")
foreach(f ${ROS_PNGS})
  list(APPEND ROS_FILES ${f})
endforeach()

set(RES_DIR "$<TARGET_FILE_DIR:RosMonDockPet>/../Resources")
add_custom_command(TARGET RosMonDockPet POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory ${RES_DIR}
  COMMAND ${CMAKE_COMMAND} -E echo "Copying runtime resources to ${RES_DIR}"
  COMMENT "Installing runtime resources into app bundle"
)
foreach(rf ${ROS_FILES})
  add_custom_command(TARGET RosMonDockPet POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${rf}" "${RES_DIR}/"
  )
endforeach()
