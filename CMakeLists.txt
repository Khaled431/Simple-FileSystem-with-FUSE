cmake_minimum_required(VERSION 3.8.2)
project(assignment3)

set(CMAKE_CXX_STANDARD 11)

add_executable(assignment3
        src/block.c
        src/block.h
        src/config.h
        src/config.h.in
        src/fuse.h
        src/log.c
        src/log.h
        src/params.h
        src/sfs.c
        src/sfs.h
        src/stamp-h1
        src/bitmap.h
        src/bitmap.c
        src/ByteBuffer.c
        src/ByteBuffer.h
        src/helper.c
        src/helper.h)
