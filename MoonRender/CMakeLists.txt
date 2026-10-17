set(CMAKE_CXX_STANDARD 20)
set(TARGET_NAME MoonRender)
file(GLOB_RECURSE ${TARGET_NAME}_impl CONFIGURE_DEPENDS  "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/*.c" "${CMAKE_CURRENT_SOURCE_DIR}/*.inl")
file(GLOB_RECURSE ${TARGET_NAME}_head CONFIGURE_DEPENDS  "${CMAKE_CURRENT_SOURCE_DIR}/*.h" "${CMAKE_CURRENT_SOURCE_DIR}/*.hpp")
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${${TARGET_NAME}_impl} ${${TARGET_NAME}_head})
add_library(${TARGET_NAME} STATIC ${${TARGET_NAME}_impl} ${${TARGET_NAME}_head})
target_include_directories(${TARGET_NAME} PUBLIC 
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    ${EIGEN3_INCLUDE_DIR}
    )
target_link_libraries(${TARGET_NAME} PUBLIC
 glad
 std_image
 tracy
 assimp
 tinyxml2
 # The shader sources packed at build time (Tools/ShaderPack): linking it is what makes
 # the engine find its shaders in the executable instead of on the disk.
 MoonShader
 )
target_compile_options(${TARGET_NAME} PRIVATE /bigobj )
target_compile_definitions(${TARGET_NAME} PUBLIC GRAPHICS_API_OPENGL)
