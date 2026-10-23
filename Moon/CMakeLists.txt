set(TARGET_NAME Moon)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
qt5_add_resources(RESOURCES ./resource/resource.qrc)
file(GLOB_RECURSE Moon_HEAD CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.h ${CMAKE_CURRENT_SOURCE_DIR}/*.hpp)
file(GLOB_RECURSE Moon_SOURCE CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${Moon_HEAD} ${Moon_SOURCE})
add_executable(${TARGET_NAME} ${Moon_HEAD} ${Moon_SOURCE} ${RESOURCES})
set_target_properties(${TARGET_NAME}
  PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
#get_target_property(TARGET_EXE_DIR ${TARGET_NAME} RUNTIME_OUTPUT_DIRECTORY)
set(TARGET_EXE_DIR "$<TARGET_FILE_DIR:${TARGET_NAME}>")
# Link dependencies    
target_link_libraries(${TARGET_NAME} PUBLIC MoonMatcLib)

target_link_libraries(${TARGET_NAME} PUBLIC
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Gui
    Qt${QT_VERSION_MAJOR}::OpenGL
    Qt${QT_VERSION_MAJOR}::Svg
)

target_link_libraries(${TARGET_NAME} PUBLIC
        spdlog
        MoonRender
        tinygltf
        MoonGeometry
)
target_include_directories(${TARGET_NAME} PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR} 
    ${EIGEN3_INCLUDE_DIR}
    ${ThirdPartyDir}
    ${LibDIR}/oidn/include
    ${LibDIR}/freetype/include
)

target_compile_options(${TARGET_NAME} PRIVATE /bigobj /utf-8)
set_target_properties(${TARGET_NAME} PROPERTIES CXX_STANDARD 20)

set(OIDN_LIBRARIES "OpenImageDenoise")
set(FREETYPE_LIBRARIES "freetype")

# Link dependencies    
target_link_directories(${TARGET_NAME}
    PRIVATE "${CMAKE_SOURCE_DIR}/lib/oidn/lib" "${CMAKE_SOURCE_DIR}/lib/freetype/lib" 
    "${CMAKE_SOURCE_DIR}/lib"
)
message(STATUS "the lib is ${CMAKE_SOURCE_DIR}/lib")
target_link_libraries(${TARGET_NAME} PUBLIC ${OIDN_LIBRARIES}  ${FREETYPE_LIBRARIES}
)

add_custom_command(
    TARGET ${TARGET_NAME}
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${LibDIR}/oidn/lib/OpenImageDenoise.dll" "${TARGET_EXE_DIR}"
    COMMENT "Copying OpenImageDenoise.dll"
)
add_custom_command(
    TARGET ${TARGET_NAME}
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${LibDIR}/freetype/bin/freetype.dll" "${TARGET_EXE_DIR}"
    COMMENT "Copying freetype.dll"
)
add_custom_command(
    TARGET ${TARGET_NAME}
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${LibDIR}/oidn/lib/tbb.dll" "${TARGET_EXE_DIR}"
    COMMENT "Copying tbb.dll"
)
file(GLOB_RECURSE RESOURCE_FILES "${ResDIR}/*")

# The engine shaders are packed into the MoonShader archive (Tools/ShaderPack), so the runtime
# does not read them from disk ("sources are read from the embedded archive"). They are still
# copied into the run directory because two things do read them:
#   - PathTraceRenderPass reads the PathTrace files itself and injects its option #defines
#     before compiling them (the permanent exception in docs/ShaderPipeline.md section 8), and
#   - the material editor edits this folder: it is the working copy it lists, reads, and
#     recompiles in process, .glsl headers included.

if(RESOURCE_FILES)  
    foreach(FILE ${RESOURCE_FILES})
        file(RELATIVE_PATH REL_FILE_PATH ${ResDIR} ${FILE})
        get_filename_component(FILE_DIR ${REL_FILE_PATH} DIRECTORY)
        set(TARGET_DIR "${TARGET_EXE_DIR}/${FILE_DIR}")
        
        add_custom_command(
            TARGET ${TARGET_NAME}
            POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "${TARGET_DIR}"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${FILE}" "${TARGET_DIR}"
            COMMENT "Copying resource file: ${REL_FILE_PATH}"
        )
    endforeach()
endif()


set(QtPluginsDir "${QT_DIR}/../../../plugins")
file(GLOB QtPluginIconEnginesDLLs  "${QtPluginsDir}/iconengines/qsvgicon*.dll")
file(GLOB QtPluginImageFormatsDLLs "${QtPluginsDir}/imageformats/qsvg*.dll")
file(GLOB QtPluginPlatformsDLLs    "${QtPluginsDir}/platforms/qwindows*.dll")
set(QtPluginsDLLs ${QtPluginIconEnginesDLLs} ${QtPluginImageFormatsDLLs} ${QtPluginPlatformsDLLs})
foreach(QtPluginDLL ${QtPluginsDLLs})
    cmake_path(GET QtPluginDLL PARENT_PATH QtPluginDLL_Path)
    cmake_path(GET QtPluginDLL_Path FILENAME QtPluginDLL_PathName)
    add_custom_command(
        TARGET ${TARGET_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${TARGET_NAME}>/plugins/${QtPluginDLL_PathName}"
    )
    add_custom_command(
        TARGET ${TARGET_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${QtPluginDLL}" "$<TARGET_FILE_DIR:${TARGET_NAME}>/plugins/${QtPluginDLL_PathName}"
    )
endforeach()
# Copy OpenCascade 3rd-party DLLs
# foreach(Occ3rdDLL ${OpenCASCADE_3RDPARTY_DLLS})
#     add_custom_command(
#         TARGET ${TARGET_NAME} POST_BUILD
#         COMMAND ${CMAKE_COMMAND} -E copy_if_different "${Occ3rdDLL}" $<TARGET_FILE_DIR:${TARGET_NAME}>
#    )
# endforeach()

# Copy runtime DLLs specified with library IMPORTED_LOCATION property
# add_custom_command(
#    TARGET ${TARGET_NAME} POST_BUILD
#     COMMAND ${CMAKE_COMMAND} -E copy_if_different
#            $<TARGET_RUNTIME_DLLS:${TARGET_NAME}> $<TARGET_FILE_DIR:${TARGET_NAME}>
#    COMMAND_EXPAND_LISTS
# )
