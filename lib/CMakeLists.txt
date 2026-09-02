# libyukino -- portable screenshots
# Copyright (C) 2026 Paper
#
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2.1 of the License, or (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this library; if not, see <https://www.gnu.org/licenses/>.

cmake_minimum_required(VERSION 3.5)

project(libyukino LANGUAGES C VERSION 0.1.0)

find_package(PkgConfig REQUIRED)

pkg_check_modules(XCB xcb xcb-image)

# Make these empty for now
set(LIBRARIES "")
set(INCLUDE "")
set(DEFINES "")

set(SRC_FILES yukino.c crc32.c adler32.c fmt/png.c fmt/ppm.c)

if(XCB_FOUND)
	list(APPEND LIBRARIES ${XCB_LINK_LIBRARIES})
	list(APPEND INCLUDE ${XCB_INCLUDE_DIRS})
	list(APPEND SRC_FILES sys/xcb.c)
	list(APPEND DEFINES "-DYUKINO_XCB=1")
endif()

add_library(libyukino ${SRC_FILES})

set_target_properties(libyukino PROPERTIES OUTPUT_NAME "yukino")

target_include_directories(libyukino PRIVATE ${INCLUDE})
target_include_directories(libyukino PUBLIC .)
target_link_libraries(libyukino PRIVATE ${LIBRARIES})
target_compile_definitions(libyukino PRIVATE ${DEFINES})
