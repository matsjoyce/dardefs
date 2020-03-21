cmake_minimum_required(VERSION 2.8)
project(fs)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}")

find_package(Crypto++ REQUIRED)
find_package(docopt REQUIRED)
find_package(PkgConfig)
pkg_check_modules(FUSE3 REQUIRED fuse3)

add_definitions(${FUSE_CFLAGS})

include(CheckCXXCompilerFlag)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# http://stackoverflow.com/a/33266748/3946766
function(enable_cxx_compiler_flag_if_supported flag)
    string(FIND "${CMAKE_CXX_FLAGS}" "${flag}" flag_already_set)
    if(flag_already_set EQUAL -1)
        check_cxx_compiler_flag("${flag}" flag_supported)
        if(flag_supported)
            set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${flag}" PARENT_SCOPE)
        endif()
    endif()
endfunction()

enable_cxx_compiler_flag_if_supported("-W")
enable_cxx_compiler_flag_if_supported("-Wall")
enable_cxx_compiler_flag_if_supported("-Wextra")
enable_cxx_compiler_flag_if_supported("-pedantic")
enable_cxx_compiler_flag_if_supported("-fdiagnostics-color=always")
add_definitions(-D_FILE_OFFSET_BITS=64)

add_library(libfs SHARED
            src/disk.cpp
            src/buffer.cpp
            src/types.cpp
            src/blockfile.cpp
            src/blocktree.cpp
            src/file.cpp
            src/dir.cpp
            src/fuse_interface.cpp
)
set_target_properties(libfs PROPERTIES PREFIX "")
target_link_libraries(libfs cryptopp docopt fuse3)

add_executable(fs src/main.cpp)
target_link_libraries(fs libfs)
