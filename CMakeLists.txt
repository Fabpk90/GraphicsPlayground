cmake_minimum_required(VERSION 3.20)
project(GraphicsPlayground)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)

add_definitions(-D PLATFORM_WIN32)

include_directories(external/include)

file(GLOB_RECURSE ENGINE_SOURCES src/*.cpp src/*.c)
file(GLOB_RECURSE ENGINE_HEADERS external/*.h external/*.hpp external/*.inl src/*.hpp)

link_directories(${CMAKE_SOURCE_DIR}/external/lib)

add_executable(GraphicsPlayground WIN32 ${ENGINE_SOURCES} ${ENGINE_HEADERS})

IF(${CMAKE_BUILD_TYPE} MATCHES Debug)
    target_link_libraries(GraphicsPlayground assimp/assimpd
            DiligentTools/DiligentToolsd DiligentCore/DiligentWin32Platformd DiligentCore/DiligentCommond DiligentCore/DiligentBasicPlatformd
            DiligentCore/DiligentGraphicsAccessoriesd DiligentCore/DiligentGraphicsToolsd)
else()
    target_link_libraries(GraphicsPlayground assimp/assimp assimp/IrrXML
            DiligentTools/DiligentTools DiligentCore/DiligentWin32Platform DiligentCore/DiligentCommon DiligentCore/DiligentBasicPlatform
            DiligentCore/DiligentGraphicsAccessories DiligentCore/DiligentGraphicsTools)
endif()