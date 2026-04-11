# Install script for directory: /Users/Lithos/Documents/GitHub/Ark-DeskPet/spine-runtimes/spine-cpp

# Set the install prefix
if(NOT DEFINED CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX "/usr/local")
endif()
string(REGEX REPLACE "/$" "" CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")

# Set the install configuration name.
if(NOT DEFINED CMAKE_INSTALL_CONFIG_NAME)
  if(BUILD_TYPE)
    string(REGEX REPLACE "^[^A-Za-z0-9_]+" ""
           CMAKE_INSTALL_CONFIG_NAME "${BUILD_TYPE}")
  else()
    set(CMAKE_INSTALL_CONFIG_NAME "")
  endif()
  message(STATUS "Install configuration: \"${CMAKE_INSTALL_CONFIG_NAME}\"")
endif()

# Set the component getting installed.
if(NOT CMAKE_INSTALL_COMPONENT)
  if(COMPONENT)
    message(STATUS "Install component: \"${COMPONENT}\"")
    set(CMAKE_INSTALL_COMPONENT "${COMPONENT}")
  else()
    set(CMAKE_INSTALL_COMPONENT)
  endif()
endif()

# Is this installation the result of a crosscompile?
if(NOT DEFINED CMAKE_CROSSCOMPILING)
  set(CMAKE_CROSSCOMPILING "FALSE")
endif()

# Set path to fallback-tool for dependency-resolution.
if(NOT DEFINED CMAKE_OBJDUMP)
  set(CMAKE_OBJDUMP "/usr/bin/objdump")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/dist/lib" TYPE STATIC_LIBRARY FILES "/Users/Lithos/Documents/GitHub/Ark-DeskPet/mac-dockpet/build/spine-cpp-build/libspine-cpp.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/dist/lib/libspine-cpp.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/dist/lib/libspine-cpp.a")
    execute_process(COMMAND "/usr/bin/ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/dist/lib/libspine-cpp.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  include("/Users/Lithos/Documents/GitHub/Ark-DeskPet/mac-dockpet/build/spine-cpp-build/CMakeFiles/spine-cpp.dir/install-cxx-module-bmi-noconfig.cmake" OPTIONAL)
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/dist/include" TYPE FILE FILES
    "/Users/Lithos/Documents/GitHub/Ark-DeskPet/spine-runtimes/spine-cpp/spine-cpp/include/spine/Animation.h"
    "/Users/Lithos/Documents/GitHub/Ark-DeskPet/spine-runtimes/spine-cpp/spine-cpp/include/spine/AnimationState.h"
    "/Users/Lithos/Documents/GitHub/Ark-DeskPet/spine-runtimes/spine-cpp/spine-cpp/include/spine/AnimationStateData.h"
    "/Users/Lithos/Documents/GitHub/Ark-DeskPet/spine-runtimes/spine-cpp/spine-cpp/include/spine/Atlas.h"
    "/Users/Lithos/Documents/GitHub/Ark-DeskPet/spine-runtimes/spine-cpp/spine-cpp/include/spine/AtlasAttachmentLoader.h"
    "/Users/Lithos/Documents/GitHub/Ark-DeskPet/spine-runtimes/spine-cpp/spine-cpp/include/spine/Attachment.h"
    "/Users/Lithos/Documents/GitHub/Ark-DeskPet/spine-runtimes/spine-cpp/spine-cpp/include/spine/AttachmentLoader.h"
    "/Users/Lithos/Documents/GitHub/Ark-DeskPet/spine-runtimes/spine-cpp/spine-cpp/include/spine/AttachmentTimeline.h"
    "/Users/Lithos/Documents/GitHub/Ark-DeskPet/spine-runtimes/spine-cpp/spine-cpp/include/spine/AttachmentType.h"
    "/Users/Lithos/Documents/GitHub/Ark-DeskPet/spine-runtimes/spine-cpp/spine-cpp/include/spine/BlendMode.h"
    "/Users/Lithos/Documents/GitHub/Ark-DeskPet/spine-runtimes/spine-cpp/spine-cpp/include/spine/Bone.h"
    "/Users/Lithos/Documents/GitHub/Ark-DeskPet/spine-runtimes/spine-cpp/spine-cpp/include/spine/BoneData.h"
    "/Users/Lithos/Documents/GitHub/Ark-DeskPet/spine-runtimes/spine-cpp/spine-cpp/include/spine/BoundingBoxAttachment.h"
    "/Users/Lithos/Documents/GitHub/Ark-DeskPet/spine-runtimes/spine-cpp/spine-cpp/include/spine/ClippingAttachment.h"
    "/Users/Lithos/Documents/GitHub/Ark-DeskPet/spine-runtimes/spine-cpp/spine-cpp/include/spine/Color.h"
    "/Users/Lithos/Documents/GitHub/Ark-DeskPet/spine-runtimes/spine-cpp/spine-cpp/include/spine/ColorTimeline.h"
    "/Users/Lithos/Documents/GitHub/Ark-DeskPet/spine-runtimes/spine-cpp/spine-cpp/include/spine/ConstraintData.h"
    "/Users/Lithos/Documents/GitHub/Ark-DeskPet/spine-runtimes/spine-cpp/spine-cpp/include/spine/ContainerUtil.h"
    "/Users/Lithos/Documents/GitHub/Ark-DeskPet/spine-runtimes/spine-cpp/spine-cpp/include/spine/CurveTimeline.h"
    "/Users/Lithos/Documents/GitHub/Ark-DeskPet/spine-runtimes/spine-cpp/spine-cpp/include/spine/Debug.h"
    "/Users/Lithos/Documents/GitHub/Ark-DeskPet/spine-runtimes/spine-cpp/spine-cpp/include/spine/DeformTimeline.h"
    "/Users/Lithos/Documents/GitHub/Ark-DeskPet/spine-runtimes/spine-cpp/spine-cpp/include/spine/DrawOrderTimeline.h"
    "/Users/Lithos/Documents/GitHub/Ark-DeskPet/spine-runtimes/spine-cpp/spine-cpp/include/spine/Event.h"
    "/Users/Lithos/Documents/GitHub/Ark-DeskPet/spine-runtimes/spine-cpp/spine-cpp/include/spine/EventData.h"
    "/Users/Lithos/Documents/GitHub/Ark-DeskPet/spine-runtimes/spine-cpp/spine-cpp/include/spine/EventTimeline.h"
    "/Users/Lithos/Documents/GitHub/Ark-DeskPet/spine-runtimes/spine-cpp/spine-cpp/include/spine/Extension.h"
    "/Users/Lithos/Documents/GitHub/Ark-DeskPet/spine-runtimes/spine-cpp/spine-cpp/include/spine/HasRendererObject.h"
    "/Users/Lithos/Documents/GitHub/Ark-DeskPet/spine-runtimes/spine-cpp/spine-cpp/include/spine/HashMap.h"
    "/Users/Lithos/Documents/GitHub/Ark-DeskPet/spine-runtimes/spine-cpp/spine-cpp/include/spine/IkConstraint.h"
    "/Users/Lithos/Documents/GitHub/Ark-DeskPet/spine-runtimes/spine-cpp/spine-cpp/include/spine/IkConstraintData.h"
    "/Users/Lithos/Documents/GitHub/Ark-DeskPet/spine-runtimes/spine-cpp/spine-cpp/include/spine/IkConstraintTimeline.h"
    "/Users/Lithos/Documents/GitHub/Ark-DeskPet/spine-runtimes/spine-cpp/spine-cpp/include/spine/Json.h"
    "/Users/Lithos/Documents/GitHub/Ark-DeskPet/spine-runtimes/spine-cpp/spine-cpp/include/spine/LinkedMesh.h"
    "/Users/Lithos/Documents/GitHub/Ark-DeskPet/spine-runtimes/spine-cpp/spine-cpp/include/spine/MathUtil.h"
    "/Users/Lithos/Documents/GitHub/Ark-DeskPet/spine-runtimes/spine-cpp/spine-cpp/include/spine/MeshAttachment.h"
    "/Users/Lithos/Documents/GitHub/Ark-DeskPet/spine-runtimes/spine-cpp/spine-cpp/include/spine/MixBlend.h"
    "/Users/Lithos/Documents/GitHub/Ark-DeskPet/spine-runtimes/spine-cpp/spine-cpp/include/spine/MixDirection.h"
    "/Users/Lithos/Documents/GitHub/Ark-DeskPet/spine-runtimes/spine-cpp/spine-cpp/include/spine/PathAttachment.h"
    "/Users/Lithos/Documents/GitHub/Ark-DeskPet/spine-runtimes/spine-cpp/spine-cpp/include/spine/PathConstraint.h"
    "/Users/Lithos/Documents/GitHub/Ark-DeskPet/spine-runtimes/spine-cpp/spine-cpp/include/spine/PathConstraintData.h"
    "/Users/Lithos/Documents/GitHub/Ark-DeskPet/spine-runtimes/spine-cpp/spine-cpp/include/spine/PathConstraintMixTimeline.h"
    "/Users/Lithos/Documents/GitHub/Ark-DeskPet/spine-runtimes/spine-cpp/spine-cpp/include/spine/PathConstraintPositionTimeline.h"
    "/Users/Lithos/Documents/GitHub/Ark-DeskPet/spine-runtimes/spine-cpp/spine-cpp/include/spine/PathConstraintSpacingTimeline.h"
    "/Users/Lithos/Documents/GitHub/Ark-DeskPet/spine-runtimes/spine-cpp/spine-cpp/include/spine/PointAttachment.h"
    "/Users/Lithos/Documents/GitHub/Ark-DeskPet/spine-runtimes/spine-cpp/spine-cpp/include/spine/Pool.h"
    "/Users/Lithos/Documents/GitHub/Ark-DeskPet/spine-runtimes/spine-cpp/spine-cpp/include/spine/PositionMode.h"
    "/Users/Lithos/Documents/GitHub/Ark-DeskPet/spine-runtimes/spine-cpp/spine-cpp/include/spine/RTTI.h"
    "/Users/Lithos/Documents/GitHub/Ark-DeskPet/spine-runtimes/spine-cpp/spine-cpp/include/spine/RegionAttachment.h"
    "/Users/Lithos/Documents/GitHub/Ark-DeskPet/spine-runtimes/spine-cpp/spine-cpp/include/spine/RotateMode.h"
    "/Users/Lithos/Documents/GitHub/Ark-DeskPet/spine-runtimes/spine-cpp/spine-cpp/include/spine/RotateTimeline.h"
    "/Users/Lithos/Documents/GitHub/Ark-DeskPet/spine-runtimes/spine-cpp/spine-cpp/include/spine/ScaleTimeline.h"
    "/Users/Lithos/Documents/GitHub/Ark-DeskPet/spine-runtimes/spine-cpp/spine-cpp/include/spine/ShearTimeline.h"
    "/Users/Lithos/Documents/GitHub/Ark-DeskPet/spine-runtimes/spine-cpp/spine-cpp/include/spine/Skeleton.h"
    "/Users/Lithos/Documents/GitHub/Ark-DeskPet/spine-runtimes/spine-cpp/spine-cpp/include/spine/SkeletonBinary.h"
    "/Users/Lithos/Documents/GitHub/Ark-DeskPet/spine-runtimes/spine-cpp/spine-cpp/include/spine/SkeletonBounds.h"
    "/Users/Lithos/Documents/GitHub/Ark-DeskPet/spine-runtimes/spine-cpp/spine-cpp/include/spine/SkeletonClipping.h"
    "/Users/Lithos/Documents/GitHub/Ark-DeskPet/spine-runtimes/spine-cpp/spine-cpp/include/spine/SkeletonData.h"
    "/Users/Lithos/Documents/GitHub/Ark-DeskPet/spine-runtimes/spine-cpp/spine-cpp/include/spine/SkeletonJson.h"
    "/Users/Lithos/Documents/GitHub/Ark-DeskPet/spine-runtimes/spine-cpp/spine-cpp/include/spine/Skin.h"
    "/Users/Lithos/Documents/GitHub/Ark-DeskPet/spine-runtimes/spine-cpp/spine-cpp/include/spine/Slot.h"
    "/Users/Lithos/Documents/GitHub/Ark-DeskPet/spine-runtimes/spine-cpp/spine-cpp/include/spine/SlotData.h"
    "/Users/Lithos/Documents/GitHub/Ark-DeskPet/spine-runtimes/spine-cpp/spine-cpp/include/spine/SpacingMode.h"
    "/Users/Lithos/Documents/GitHub/Ark-DeskPet/spine-runtimes/spine-cpp/spine-cpp/include/spine/SpineObject.h"
    "/Users/Lithos/Documents/GitHub/Ark-DeskPet/spine-runtimes/spine-cpp/spine-cpp/include/spine/SpineString.h"
    "/Users/Lithos/Documents/GitHub/Ark-DeskPet/spine-runtimes/spine-cpp/spine-cpp/include/spine/TextureLoader.h"
    "/Users/Lithos/Documents/GitHub/Ark-DeskPet/spine-runtimes/spine-cpp/spine-cpp/include/spine/Timeline.h"
    "/Users/Lithos/Documents/GitHub/Ark-DeskPet/spine-runtimes/spine-cpp/spine-cpp/include/spine/TimelineType.h"
    "/Users/Lithos/Documents/GitHub/Ark-DeskPet/spine-runtimes/spine-cpp/spine-cpp/include/spine/TransformConstraint.h"
    "/Users/Lithos/Documents/GitHub/Ark-DeskPet/spine-runtimes/spine-cpp/spine-cpp/include/spine/TransformConstraintData.h"
    "/Users/Lithos/Documents/GitHub/Ark-DeskPet/spine-runtimes/spine-cpp/spine-cpp/include/spine/TransformConstraintTimeline.h"
    "/Users/Lithos/Documents/GitHub/Ark-DeskPet/spine-runtimes/spine-cpp/spine-cpp/include/spine/TransformMode.h"
    "/Users/Lithos/Documents/GitHub/Ark-DeskPet/spine-runtimes/spine-cpp/spine-cpp/include/spine/TranslateTimeline.h"
    "/Users/Lithos/Documents/GitHub/Ark-DeskPet/spine-runtimes/spine-cpp/spine-cpp/include/spine/Triangulator.h"
    "/Users/Lithos/Documents/GitHub/Ark-DeskPet/spine-runtimes/spine-cpp/spine-cpp/include/spine/TwoColorTimeline.h"
    "/Users/Lithos/Documents/GitHub/Ark-DeskPet/spine-runtimes/spine-cpp/spine-cpp/include/spine/Updatable.h"
    "/Users/Lithos/Documents/GitHub/Ark-DeskPet/spine-runtimes/spine-cpp/spine-cpp/include/spine/Vector.h"
    "/Users/Lithos/Documents/GitHub/Ark-DeskPet/spine-runtimes/spine-cpp/spine-cpp/include/spine/VertexAttachment.h"
    "/Users/Lithos/Documents/GitHub/Ark-DeskPet/spine-runtimes/spine-cpp/spine-cpp/include/spine/VertexEffect.h"
    "/Users/Lithos/Documents/GitHub/Ark-DeskPet/spine-runtimes/spine-cpp/spine-cpp/include/spine/Vertices.h"
    "/Users/Lithos/Documents/GitHub/Ark-DeskPet/spine-runtimes/spine-cpp/spine-cpp/include/spine/dll.h"
    "/Users/Lithos/Documents/GitHub/Ark-DeskPet/spine-runtimes/spine-cpp/spine-cpp/include/spine/spine.h"
    )
endif()

string(REPLACE ";" "\n" CMAKE_INSTALL_MANIFEST_CONTENT
       "${CMAKE_INSTALL_MANIFEST_FILES}")
if(CMAKE_INSTALL_LOCAL_ONLY)
  file(WRITE "/Users/Lithos/Documents/GitHub/Ark-DeskPet/mac-dockpet/build/spine-cpp-build/install_local_manifest.txt"
     "${CMAKE_INSTALL_MANIFEST_CONTENT}")
endif()
