cmake_minimum_required (VERSION 3.6)
set(DILIGENT_NO_OPENGL 1)
set(DILIGENT_NO_DIRECT3D11 1)
set(DILIGENT_LOAD_PIX_EVENT_RUNTIME 1)

set(CMAKE_BUILD_PARALLEL_LEVEL 1)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)

#set(CMAKE_UNITY_BUILD 1)

project(GraphicsPlayground)

add_definitions(-D PLATFORM_WIN32 -D DILIGENT_LOAD_PIX_EVENT_RUNTIME)
add_compile_definitions(USE_PIX=1)
add_compile_definitions(DILIGENT_LOAD_PIX_EVENT_RUNTIME=1)

include_directories(external/include)
include_directories(external/DiligentCore)
include_directories(external/DiligentTools)
include_directories(external/RenderDoc)

add_subdirectory(external/DiligentCore)
add_subdirectory(external/DiligentTools)

add_subdirectory(external/mimalloc)
include_directories(external/mimalloc/include)

include_directories(external/taskflow/)
include_directories(external/WinPix/)
include_directories(external/include/zstd/)
include_directories(external/)

add_subdirectory(external/QuikMafs)

find_package(flatbuffers CONFIG REQUIRED)

file(GLOB_RECURSE ENGINE_SOURCES src/*.cpp src/*.c)
file(GLOB_RECURSE ENGINE_HEADERS external/*.h external/*.hpp external/*.inl /src/*.hpp src/util/*.h src/util/*.hpp)

link_directories(${CMAKE_SOURCE_DIR}/external/lib)

set(VCPKG_TARGET_TRIPLET x64-windows)
IF(${CMAKE_BUILD_TYPE} MATCHES Debug)
    set(VCPKG_BUILD_TYPE debug)
ENDIF()

add_executable(GraphicsPlayground WIN32 ${ENGINE_SOURCES} ${ENGINE_HEADERS})

target_compile_options(GraphicsPlayground PRIVATE -DUNICODE -DENGINE_DLL)

target_include_directories(GraphicsPlayground PRIVATE "DiligentCore")
target_include_directories(GraphicsPlayground PRIVATE "DiligentTools")
target_include_directories(GraphicsPlayground PRIVATE "meshoptimizer")

find_package(Vulkan)
find_package(EASTL CONFIG REQUIRED)
find_package(assimp CONFIG REQUIRED)
find_package(meshoptimizer CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
add_custom_target(NatVis SOURCES doc/EASTL.natvis)
IF(${RETAIL})
    #add_definitions(-D FINAL 1)
    option(TRACY_ENABLE "" OFF)
    #option(TRACY_ON_DEMAND "" OFF)
else()
    #set options before add_subdirectory#available options: TRACY_ENABLE , TRACY_ON_DEMAND , TRACY_NO_BROADCAST ,TRACY_NO_CODE_TRANSFER , ...
    option(TRACY_ENABLE "" ON)
    option(TRACY_ON_DEMAND "" ON)
      # target: TracyClientoralias Tracy::TracyClient
endif()

add_subdirectory(external/Tracy)

IF(${CMAKE_BUILD_TYPE} MATCHES Debug)
    target_link_libraries(GraphicsPlayground #assimp/assimp-vc143-mtd
            assimp::assimp
            Tracy::TracyClient
            #Diligent-GraphicsEngineD3D11-shared
            #Diligent-GraphicsEngineOpenGL-shared
            Diligent-GraphicsEngineD3D12-shared
            Diligent-GraphicsEngineVk-shared
            Diligent-Imgui
            Diligent-TextureLoader

            EASTL
            mimalloc-static
            meshoptimizer::meshoptimizer

            flatbuffers::flatbuffers

            #DiligentTools/DiligentToolsd DiligentCore/DiligentWin32Platformd DiligentCore/DiligentCommond DiligentCore/DiligentBasicPlatformd
            #DiligentCore/DiligentGraphicsAccessoriesd DiligentCore/DiligentGraphicsToolsd)
            )
else()
target_link_libraries(GraphicsPlayground #assimp/assimp-vc143-mt
        assimp::assimp
        Tracy::TracyClient
        #Diligent-GraphicsEngineD3D11-shared
        #Diligent-GraphicsEngineOpenGL-shared
        Diligent-GraphicsEngineD3D12-shared
        Diligent-GraphicsEngineVk-shared
        Diligent-Imgui
        Diligent-TextureLoader

        flatbuffers::flatbuffers

        EASTL
        mimalloc-static
        meshoptimizer::meshoptimizer
          #DiligentTools/DiligentTools DiligentCore/DiligentWin32Platform DiligentCore/DiligentCommon DiligentCore/DiligentBasicPlatform
           # DiligentCore/DiligentGraphicsAccessories DiligentCore/DiligentGraphicsTools)
        )
endif()
copy_required_dlls(GraphicsPlayground)