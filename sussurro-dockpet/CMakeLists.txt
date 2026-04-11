cmake_minimum_required(VERSION 3.16)
project(SussurroDockPet LANGUAGES C CXX OBJCXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# SFML: prefer 3.x if available, otherwise try 2.5
set(SFML3_FOUND FALSE)
find_package(SFML 3 COMPONENTS Graphics Window System QUIET)
if(SFML_FOUND)
  set(SFML3_FOUND TRUE)
  message(STATUS "Found SFML 3.x")
else()
  find_package(SFML 2.5 COMPONENTS graphics window system REQUIRED)
endif()

# Spine CPP runtime
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../spine-runtimes/spine-cpp ${CMAKE_BINARY_DIR}/spine-cpp-build)
if(TARGET spine-cpp)
  message(STATUS "Enabling RTTI and exceptions for spine-cpp target")
  target_compile_options(spine-cpp PUBLIC -frtti -fexceptions)
endif()

# App
add_executable(SussurroDockPet
  sussurro_main.cpp
  ../mac-dockpet/MacWindowUtils.mm
)

# macOS bundle setup similar to RosMonDockPet
set_target_properties(SussurroDockPet PROPERTIES
  MACOSX_BUNDLE TRUE
)

# Include directories
target_include_directories(SussurroDockPet PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../spine-runtimes/spine-cpp/spine-cpp/include
)

# Link libraries
if(SFML3_FOUND)
  target_link_libraries(SussurroDockPet PRIVATE SFML::Graphics SFML::Window SFML::System spine-cpp "-framework Cocoa" "-framework OpenGL")
else()
  target_link_libraries(SussurroDockPet PRIVATE sfml-graphics sfml-window sfml-system spine-cpp "-framework Cocoa" "-framework OpenGL")
endif()

# Copy assets into the app bundle Resources
set(SUSSURRO_ASSETS
  "${CMAKE_CURRENT_SOURCE_DIR}/../Sussurro/build_char_298_susuro_summer#6.atlas"
  "${CMAKE_CURRENT_SOURCE_DIR}/../Sussurro/build_char_298_susuro_summer#6.skel"
  "${CMAKE_CURRENT_SOURCE_DIR}/../Sussurro/build_char_298_susuro_summer#6.png"
)

add_custom_command(TARGET SussurroDockPet POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:SussurroDockPet>/../Resources"
  COMMAND ${CMAKE_COMMAND} -E copy ${SUSSURRO_ASSETS} "$<TARGET_FILE_DIR:SussurroDockPet>/../Resources"
  COMMENT "Installing Sussurro assets into app bundle"
)
