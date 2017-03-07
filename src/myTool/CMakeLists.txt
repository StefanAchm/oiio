set(sourceOne "${PROJECT_SOURCE_DIR}/build/src/libOpenImageIO/Debug")
set(sourceTwo "${PROJECT_SOURCE_DIR}/../dll")
set(destination "${PROJECT_SOURCE_DIR}/build/src/myTool/Debug")

set (myTool_srcs myTool.cpp)
add_executable (myTool ${myTool_srcs})
add_custom_command(TARGET myTool
                   PRE_BUILD
                   COMMAND Xcopy ARGS "/Y /F /C \"${sourceOne}/OpenImageIO.*\" \"${destination}\""
				   COMMAND Xcopy ARGS "/Y /F /C \"${sourceTwo}\" \"${destination}\"")
set_target_properties (myTool PROPERTIES FOLDER "Tools")
target_link_libraries (myTool OpenImageIO ${SANITIZE_LIBRARIES} ${Boost_LIBRARIES} ${CMAKE_DL_LIBS})
oiio_install_targets (myTool)
