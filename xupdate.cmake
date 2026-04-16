include_directories(${CMAKE_CURRENT_LIST_DIR})

include(${CMAKE_CURRENT_LIST_DIR}/../XArchive/xarchive.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/../XGithub/xgithub.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/../XOptions/xoptions.cmake)

set(XUPDATE_SOURCES
    ${CMAKE_CURRENT_LIST_DIR}/xupdate.cpp
    ${XARCHIVE_SOURCES}
    ${XGITHUB_SOURCES}
    ${XOPTIONS_SOURCES}
)
