cmake_minimum_required(VERSION 3.28)
project(dils)

set(CMAKE_CXX_STANDARD 17)

include_directories(include)

add_executable(dils
        src/dils.cpp
        src/driver.cpp
        src/bitmap.cpp
        include/driver.h
        include/bitmap.h
        include/sfs_inode.h
        include/sfs_superblock.h
        include/sfs_dir.h)
