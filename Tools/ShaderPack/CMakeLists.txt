set(TARGET_NAME ShaderPack)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# The archive target has generated sources, and the project turns AUTOMOC on globally:
# without this the generated files are quietly excluded from it and CMake warns.
if (POLICY CMP0071)
  cmake_policy(SET CMP0071 NEW)
endif()

file(GLOB_RECURSE ${TARGET_NAME}_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
add_executable(${TARGET_NAME} ${${TARGET_NAME}_SOURCE})
set_target_properties(${TARGET_NAME}
  PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
set_target_properties(${TARGET_NAME} PROPERTIES CXX_STANDARD 20)

# ---------------------------------------------------------------------------------------
# The shader archive
#
# Two steps, the same split filament uses (matc, then resgen):
#
#   1. MoonMatc, once per .shader: parse the material file, resolve the prelude, validate and
#      emit one .ovshader package per shader (every variant, every stage, plus reflection).
#      A .shader that has not been migrated yet (no material { } block) is copied through as
#      it is, so the shaders can move over one at a time.
#   2. ShaderPack, once: concatenate every package - and the .glsl the not-yet-migrated
#      shaders still #include at run time - into one blob compiled into the MoonShader
#      library, which MoonRender links.
#
# CONFIGURE_DEPENDS so that adding a shader does not need a manual reconfigure.
# ---------------------------------------------------------------------------------------
set(SHADER_SOURCE_DIR "${CMAKE_SOURCE_DIR}/Resource/Moon/Data/Engine/Shaders")
set(SHADER_OUTPUT_DIR "${CMAKE_BINARY_DIR}/generated/shaders")
set(SHADER_MATC_DIR "${SHADER_OUTPUT_DIR}/matc")
file(MAKE_DIRECTORY "${SHADER_OUTPUT_DIR}")

file(GLOB_RECURSE SHADER_SOURCES CONFIGURE_DEPENDS RELATIVE "${SHADER_SOURCE_DIR}"
     "${SHADER_SOURCE_DIR}/*.shader")
file(GLOB_RECURSE SHADER_HEADERS CONFIGURE_DEPENDS RELATIVE "${SHADER_SOURCE_DIR}"
     "${SHADER_SOURCE_DIR}/*.glsl")

set(SHADER_HEADER_PATHS)
foreach(header ${SHADER_HEADERS})
  list(APPEND SHADER_HEADER_PATHS "${SHADER_SOURCE_DIR}/${header}")
endforeach()

# Build-time validation: every stage MoonMatc generates is handed to a real GLSL compiler, so a
# broken shader stops the build instead of failing when the material is first loaded at run
# time. filament gets this for free by linking glslang into filamat; the external validator is
# the cheap equivalent, and optional: without it the pipeline behaves as before, with a note.
#
#   1. MoonMatc --dump-glsl  -> one .glsl per (pass, variant, stage)
#   2. ValidateGlslStages.cmake -> glslangValidator -S vert|frag|geom on each of them
find_program(GLSLANG_VALIDATOR NAMES glslangValidator glslang)
if(GLSLANG_VALIDATOR)
  message(STATUS "Shader stages are validated at build time with ${GLSLANG_VALIDATOR}")
  set(SHADER_VALIDATE_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/ValidateGlslStages.cmake")
  set(SHADER_GLSL_DIR "${SHADER_OUTPUT_DIR}/glsl")
else()
  message(STATUS "glslangValidator not found - shader stages will NOT be validated at build time")
endif()

# Step 1: one package per shader. Every header is a dependency - MoonMatc splices them in.
set(SHADER_PACK_ARGS)
set(SHADER_MATC_OUTPUTS)
foreach(source ${SHADER_SOURCES})
  set(package "${SHADER_MATC_DIR}/${source}")
  set(moonmatc_arguments -i "${SHADER_SOURCE_DIR}/${source}" -o "${package}" -I "${SHADER_SOURCE_DIR}")
  set(rule_commands "")
  if(GLSLANG_VALIDATOR)
    set(dump_dir "${SHADER_GLSL_DIR}/${source}")
    list(APPEND moonmatc_arguments --dump-glsl "${dump_dir}")
    # Clear the dump first: a stage this build no longer produces must not be validated (a
    # stale file could fail on something that is already fixed).
    list(APPEND rule_commands COMMAND ${CMAKE_COMMAND} -E remove_directory "${dump_dir}")
  endif()
  list(APPEND rule_commands COMMAND MoonMatc ${moonmatc_arguments})
  if(GLSLANG_VALIDATOR)
    list(APPEND rule_commands
      COMMAND ${CMAKE_COMMAND}
              "-DGLSLANG=${GLSLANG_VALIDATOR}" "-DSTAGE_DIR=${dump_dir}" "-DSHADER=${source}"
              -P "${SHADER_VALIDATE_SCRIPT}")
  endif()
  add_custom_command(
    OUTPUT "${package}"
    ${rule_commands}
    DEPENDS MoonMatc "${SHADER_SOURCE_DIR}/${source}" ${SHADER_HEADER_PATHS}
            ${SHADER_VALIDATE_SCRIPT}
    COMMENT "MoonMatc ${source}"
    VERBATIM)
  list(APPEND SHADER_MATC_OUTPUTS "${package}")
  list(APPEND SHADER_PACK_ARGS "${source}=${package}")
  # The compiled package is what the runtime loads; the editor still needs the source text
  # (release builds no longer ship the Shaders folder), so it rides along under Source/.
  list(APPEND SHADER_PACK_ARGS "Source/${source}=${SHADER_SOURCE_DIR}/${source}")
endforeach()
foreach(header ${SHADER_HEADERS})
  list(APPEND SHADER_PACK_ARGS "${header}=${SHADER_SOURCE_DIR}/${header}")
endforeach()

set(SHADER_GENERATED_HEADER "${SHADER_OUTPUT_DIR}/shader.h")
set(SHADER_GENERATED_SOURCE "${SHADER_OUTPUT_DIR}/shader.cpp")

# Step 2: all of them into one archive.
add_custom_command(
  OUTPUT "${SHADER_GENERATED_HEADER}" "${SHADER_GENERATED_SOURCE}"
  COMMAND ShaderPack -o "${SHADER_OUTPUT_DIR}" -n MOONSHADER ${SHADER_PACK_ARGS}
  # The headers are listed too: a .glsl-only edit has to regenerate shader.cpp, otherwise
  # MoonShader happily relinks the stale archive and the change never reaches the exe.
  DEPENDS ShaderPack ${SHADER_MATC_OUTPUTS} ${SHADER_HEADER_PATHS}
  COMMENT "Packing the shader packages into shader.cpp"
  VERBATIM)

add_library(MoonShader STATIC "${SHADER_GENERATED_SOURCE}" "${SHADER_GENERATED_HEADER}")
set_source_files_properties("${SHADER_GENERATED_SOURCE}" PROPERTIES GENERATED TRUE)
target_include_directories(MoonShader PUBLIC
  "${SHADER_OUTPUT_DIR}"
  "${CMAKE_SOURCE_DIR}/MoonRender/include")
target_compile_options(MoonShader PRIVATE /bigobj)
set_target_properties(MoonShader PROPERTIES CXX_STANDARD 20)

# The shader sources in the IDE.
#
# What this target builds is a generated shader.cpp, so without this the .shader/.glsl tree -
# the thing that actually gets edited - is invisible in the solution: VS only shows what a
# target lists. They are attached header-only, so no compiler ever looks at them; they are
# there to be opened, and to be visible as the inputs the custom commands above rebuild from.
set(SHADER_IDE_FILES)
foreach(source ${SHADER_SOURCES})
  list(APPEND SHADER_IDE_FILES "${SHADER_SOURCE_DIR}/${source}")
endforeach()
foreach(header ${SHADER_HEADERS})
  list(APPEND SHADER_IDE_FILES "${SHADER_SOURCE_DIR}/${header}")
endforeach()
set_source_files_properties(${SHADER_IDE_FILES} PROPERTIES HEADER_FILE_ONLY TRUE)
target_sources(MoonShader PRIVATE ${SHADER_IDE_FILES})
source_group(TREE "${SHADER_SOURCE_DIR}" PREFIX "Shaders" FILES ${SHADER_IDE_FILES})
