# copy the example to the binary tree where they can be built manually using xcode

#configure a script for making vtk a framework as the iOS examples will use the framework
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/make_vtk_framework.sh.in
               ${CMAKE_CURRENT_BINARY_DIR}/make_vtk_framework.sh)


if (TRUE)
  add_custom_target(iOS-examples-rm ALL
    COMMAND ${CMAKE_COMMAND} -E remove_directory
            "${CMAKE_CURRENT_BINARY_DIR}/GLPaint")
  add_custom_target(iOS-examples-copy ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/GLPaint"
            "${CMAKE_CURRENT_BINARY_DIR}/GLPaint")
  add_dependencies(iOS-examples-copy iOS-examples-rm)
else ()
  add_subdirectory(MaceVTK/MaceVTK)
  add_custom_target(iOS-examples-copy ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/MaceVTK"
            "${CMAKE_CURRENT_BINARY_DIR}/MaceVTK")
endif ()