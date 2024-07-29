cmake_minimum_required(VERSION 3.22.1)
project(SimpleStore)

set(CMAKE_BUILD_TYPE "Debug")

find_library(LIBBLKID blkid)
find_library(LIBAIO aio)
link_libraries(${LIBBLKID} ${LIBAIO})

aux_source_directory(. DIR_SRCS)
add_executable(SimpleStore ${DIR_SRCS})
