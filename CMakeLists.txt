cmake_minimum_required(VERSION 3.8)
project(UseQt LANGUAGES C CXX)

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)
if(NOT DEFINED EIGEN3_INCLUDE_DIR)
  set(EIGEN3_INCLUDE_DIR "${PROJECT_SOURCE_DIR}/Extern/eigen-3.4.0")
endif()
if(NOT DEFINED BOOST_INCLUDE_DIR)
  set(BOOST_INCLUDE_DIR "${PROJECT_SOURCE_DIR}/Extern/boost")
endif()
find_package(Eigen3 3.4.0 REQUIRED)
find_package(QT NAMES Qt5 REQUIRED COMPONENTS Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Gui OpenGL Svg)
message(STATUS "QT_VERSION: ${QT_VERSION}, QT_DIR: ${QT_DIR}")

if (${QT_VERSION} VERSION_LESS 5.11.0)
  message(FATAL_ERROR "Requires qt version >= 5.11.0, Your current version is ${QT_VERSION}")
endif()

#SET_PROPERTY(GLOBAL PROPERTY AUTOGEN_SOURCE_GROUP auto_gen)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

option (TRACY_ENABLE "" ON)
option (TRACY_ON_DEMAND "" ON)
set(ThirdPartyDir "${PROJECT_SOURCE_DIR}/Extern")
set(LibDIR "${PROJECT_SOURCE_DIR}/lib")
set(ResDIR "${PROJECT_SOURCE_DIR}/Resource")

##########
# OpenCascade
##########

if(WIN32)
    # Can't use find_package(OpenCASCADE) as OpenCASCADEConfig.cmake is broken for Windows platform
    # See https://dev.opencascade.org/content/3rdparty-hard-coded-paths-when-building-against-opencascade
    include(${PROJECT_SOURCE_DIR}/cmake/OpenCascadeWin.cmake)
    if (OpenCASCADE_FOUND)
        # Add OpenCASCADE include directory
        list(APPEND Mayo_IncludeDirectories ${OpenCASCADE_INCLUDE_DIR})

        # Add OpenCASCADE library directories
        # Note: QtCreator option "Run->Add build library search to PATH" will add to PATH env variable
        #       the contents of MayoCore_LinkDirectories variable. For convenience, let's add also the
        #       directories containing DLLs
        list(
            APPEND MayoCore_LinkDirectories
            ${OpenCASCADE_BINARY_DIR}
            ${OpenCASCADE_LIBRARY_DIR}
            ${OpenCASCADE_3RDPARTY_BINARY_DIRS}
        )    
     endif()
else()

endif()

list(APPEND Mayo_CompileDefinitions OCCT_HANDLE_NOCAST)

if(OpenCASCADE_FOUND)
    message(STATUS "OpenCascade version ${OpenCASCADE_VERSION}")

    # std::iterator class template(used as a base class to provide typedefs) is deprecated in C++17
    # OpenCascade < 7.7.0 uses std::iterator for NCollection_StlIterator and this is causing many
    # deprecation warnings
    if(OpenCASCADE_VERSION VERSION_LESS 7.7.0)
        if(MSVC)
            # Silent warnings C4996 and STL4015
            list(APPEND Mayo_CompileDefinitions _SILENCE_CXX17_ITERATOR_BASE_CLASS_DEPRECATION_WARNING)
        elseif((CMAKE_CXX_COMPILER_ID MATCHES "GNU") AND (CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 12))
            list(APPEND Mayo_CompileOptions -Wno-deprecated-declarations)
        endif()
    endif()

# Add OpenCASCADE libraries
list(
    APPEND MayoCore_LinkLibraries
    # FoundationClasses
    TKernel TKMath TKOffset TKFeat TKFillet
    # ModelingData
    TKG2d TKG3d TKGeomBase TKBRep
    # ModelingAlgorithms
    TKBO TKBool TKGeomAlgo TKHLR TKMesh TKPrim TKShHealing TKTopAlgo
    # Visualization
    TKMeshVS TKOpenGl TKService TKV3d
    # ApplicationFramework
    TKBin TKBinL TKBinXCAF TKCAF TKCDF TKLCAF TKVCAF TKXml TKXmlL
    # DataExchange
    TKXCAF TKXmlXCAF TKXSBase
)

    if(OpenCASCADE_VERSION VERSION_GREATER_EQUAL 7.8.0)
        list(APPEND MayoIO_LinkLibraries TKDE)
        list(APPEND MayoIO_LinkLibraries TKDEIGES)
        list(APPEND MayoIO_LinkLibraries TKDESTEP)
        list(APPEND MayoIO_LinkLibraries TKDESTL)
        list(APPEND MayoIO_LinkLibraries TKDEVRML)
    else()
        list(APPEND MayoIO_LinkLibraries TKIGES TKXDEIGES)
        list(APPEND MayoIO_LinkLibraries TKSTEP TKSTEP209 TKSTEPAttr TKSTEPBase TKXDESTEP)
        list(APPEND MayoIO_LinkLibraries TKSTL)
        list(APPEND MayoIO_LinkLibraries TKVRML)
        if(OpenCASCADE_VERSION VERSION_GREATER_EQUAL 7.7.0)
            list(APPEND MayoIO_LinkLibraries TKXDE)
        endif()
    endif()

    # OBJ/glTF support
    if(OpenCASCADE_VERSION VERSION_GREATER_EQUAL 7.4.0)
        list(APPEND MayoIO_LinkLibraries TKRWMesh)
        if(OpenCASCADE_VERSION VERSION_GREATER_EQUAL 7.8.0)
            list(APPEND MayoIO_LinkLibraries TKDEOBJ TKDEGLTF)
        endif()
    endif()

endif()

add_subdirectory(Extern)
# Tools first: Moon links MoonMatcLib (the editor compiles materials in process), and the shader
# archive (MoonShader) is what MoonRender links, so both have to exist before MoonRender/Moon.
add_subdirectory(Tools)
add_subdirectory(Moon)
add_subdirectory(MoonRender)
add_subdirectory(MoonGeomerty)
add_subdirectory(MoonTracer)
