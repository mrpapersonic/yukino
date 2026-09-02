cmake_minimum_required(VERSION 3.5)

project(yukino LANGUAGES C VERSION 0.1.0)

find_package(PkgConfig REQUIRED)

pkg_check_modules(SDL3 REQUIRED sdl3)

add_subdirectory(lib)

set(LIBRARIES libyukino ${SDL3_LINK_LIBRARIES})

set(INCLUDE lib ${SDL3_INCLUDE_DIRS})

set(SRC_FILES src/main.c)

if(XCB_FOUND)
	list(APPEND LIBRARIES ${XCB_LINK_LIBRARIES})
	list(APPEND INCLUDE ${XCB_INCLUDE_DIRS})
endif()

add_executable(yukino src/main.c)
set_property(TARGET yukino PROPERTY C_STANDARD 99)

target_include_directories(yukino PRIVATE ${INCLUDE})
target_link_libraries(yukino PRIVATE ${LIBRARIES})
