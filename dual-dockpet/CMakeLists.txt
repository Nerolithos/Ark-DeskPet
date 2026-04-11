cmake_minimum_required(VERSION 3.15)
project(DualPetsDockPet LANGUAGES C CXX OBJCXX)

if(NOT APPLE)
  message(FATAL_ERROR "This target is intended to be built on macOS (APPLE).")
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(SFML REQUIRED sfml-graphics sfml-window sfml-system)

set(SPINE_RUNTIMES_DIR "${CMAKE_SOURCE_DIR}/../spine-runtimes/spine-cpp")
set(SPINE_CPP_BIN_DIR "${CMAKE_BINARY_DIR}/spine-cpp-build")

add_subdirectory(${SPINE_RUNTIMES_DIR} ${SPINE_CPP_BIN_DIR})
if(TARGET spine-cpp)
  message(STATUS "Enabling RTTI and exceptions for spine-cpp target to work with C++ RTTI in this project")
  target_compile_options(spine-cpp PUBLIC "-frtti" "-fexceptions")
endif()

set(SOURCES
  ../run_dual_pets.cpp
  ../mac-dockpet/MacWindowUtils.mm
)

add_executable(DualPetsDockPet ${SOURCES})

target_include_directories(DualPetsDockPet PRIVATE
  ${SFML_INCLUDE_DIRS}
  ${CMAKE_SOURCE_DIR}/..
  ${CMAKE_SOURCE_DIR}/../mac-dockpet
  ${CMAKE_SOURCE_DIR}/../spine-runtimes/spine-cpp/spine-cpp/include
)
target_compile_features(DualPetsDockPet PRIVATE cxx_std_17)

target_link_libraries(DualPetsDockPet PRIVATE ${SFML_LIBRARIES} "-framework Cocoa" "-framework OpenGL")

# Link spine-cpp runtime
target_link_libraries(DualPetsDockPet PRIVATE spine-cpp)

# Silence macOS OpenGL deprecation warnings
target_compile_definitions(DualPetsDockPet PRIVATE GL_SILENCE_DEPRECATION)

if(SFML_LDFLAGS)
  target_link_options(DualPetsDockPet PRIVATE ${SFML_LDFLAGS})
endif()

set_target_properties(DualPetsDockPet PROPERTIES
  MACOSX_BUNDLE TRUE
  XCODE_ATTRIBUTE_CLANG_CXX_LIBRARY "libc++"
)

# Copy runtime resources (rosmon + sussurro + background) into the app bundle Resources
file(GLOB ROS_PNGS
  "${CMAKE_SOURCE_DIR}/../*rosmon*.png"
  "${CMAKE_SOURCE_DIR}/../build_char_*rosmon*.png"
)
if(EXISTS "${CMAKE_SOURCE_DIR}/../background.png")
  list(APPEND ROS_PNGS "${CMAKE_SOURCE_DIR}/../background.png")
endif()

set(ROS_FILES
  "${CMAKE_SOURCE_DIR}/../rosmon.atlas"
  "${CMAKE_SOURCE_DIR}/../rosmon.skel"
)
foreach(f ${ROS_PNGS})
  list(APPEND ROS_FILES ${f})
endforeach()

# Sussurro resources (atlas/skel/png)
set(SUS_FILES
  "${CMAKE_SOURCE_DIR}/../Sussurro/build_char_298_susuro_summer#6.atlas"
  "${CMAKE_SOURCE_DIR}/../Sussurro/build_char_298_susuro_summer#6.skel"
  "${CMAKE_SOURCE_DIR}/../Sussurro/build_char_298_susuro_summer#6.png"
)

set(RES_DIR "$<TARGET_FILE_DIR:DualPetsDockPet>/../Resources")
add_custom_command(TARGET DualPetsDockPet POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory ${RES_DIR}
  COMMAND ${CMAKE_COMMAND} -E echo "Copying runtime resources to ${RES_DIR}"
  COMMENT "Installing runtime resources into DualPets app bundle"
)

foreach(rf ${ROS_FILES} ${SUS_FILES})
  add_custom_command(TARGET DualPetsDockPet POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${rf}" "${RES_DIR}/"
  )
endforeach()
