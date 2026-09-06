cmake_minimum_required(VERSION 3.7)
project(Filters)

include(filters.cmake)

add_definitions(-fpic)

SET(SIMPLEIMG_SRC
        ${CMAKE_CURRENT_SOURCE_DIR}/dec_simpleimg.c
        ${CMAKE_CURRENT_SOURCE_DIR}/simpleimg.c
)

# No third-party archive: the decoders are in simpleimg.c.
SET(SIMPLEIMG_LIBS)

SET(SIMPLEIMG_INC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

add_filter(simpleimg
        "${SIMPLEIMG_SRC}"
        "${SIMPLEIMG_LIBS}"
        []
        ""
        "${SIMPLEIMG_INC}"
        ""
        "1")
