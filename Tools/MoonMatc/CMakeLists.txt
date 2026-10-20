set(TARGET_NAME MoonMatc)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Two targets: the library the editor links (it compiles a material the user is editing, and
# hands the package bytes straight to the runtime), and the command line tool the build drives
# once per .mat.
add_library(MoonMatcLib STATIC ${CMAKE_CURRENT_SOURCE_DIR}/src/Matc.cpp)
target_include_directories(MoonMatcLib PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/MoonRender/include
)
set_target_properties(MoonMatcLib
  PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
    CXX_STANDARD 20
)

add_executable(${TARGET_NAME} ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp)
target_link_libraries(${TARGET_NAME} PRIVATE MoonMatcLib)
set_target_properties(${TARGET_NAME}
  PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    CXX_STANDARD 20
)
