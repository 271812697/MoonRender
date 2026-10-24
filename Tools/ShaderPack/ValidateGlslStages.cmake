# Validates the GLSL MoonMatc just generated, stage by stage, with an external glslangValidator.
#
# Run in script mode, from the MoonMatc custom command (see CMakeLists.txt in this directory):
#
#   cmake -DGLSLANG=<glslangValidator> -DSTAGE_DIR=<dir> -DSHADER=<name.shader> -P ValidateGlslStages.cmake
#
# This is the build-time half that filament's matc gets for free by linking glslang: a shader
# that does not compile stops the build instead of failing at run time when the material is
# first loaded. MoonMatc itself stays a text tool - it emits every (pass, variant, stage) with
# `--dump-glsl` and this script hands each one to the driver-agnostic validator.
#
# The dumped stage names end in _vertex / _fragment / _geometry, and the file's own #line
# directives (plus the GL_GOOGLE_cpp_style_line_directive extension MoonMatc emits) make any
# error point back at the .shader / .glsl line that caused it.

if(NOT GLSLANG OR NOT STAGE_DIR)
  message(FATAL_ERROR "ValidateGlslStages.cmake: GLSLANG and STAGE_DIR are required")
endif()
if(NOT EXISTS "${GLSLANG}")
  message(FATAL_ERROR "ValidateGlslStages.cmake: no glslangValidator at ${GLSLANG}")
endif()

file(GLOB stage_files "${STAGE_DIR}/*.glsl")
if(NOT stage_files)
  # A shader that is not a material yet (no material { } block) is copied through by MoonMatc
  # and dumps nothing - the PathTrace four, until they are refactored. Nothing to validate.
  message(STATUS "${SHADER}: not a material (passthrough), no stages to validate")
  return()
endif()

set(failed 0)
foreach(stage_file IN LISTS stage_files)
  get_filename_component(stage_name "${stage_file}" NAME_WE)

  # Match on the suffix only, case sensitively: a feature name such as VERTEX_POS_NORMAL
  # contains "vertex" too, and a case-insensitive match would compile fragments as vertices.
  if(stage_name MATCHES "_geometry$")
    set(stage_flag geom)
  elseif(stage_name MATCHES "_vertex$")
    set(stage_flag vert)
  elseif(stage_name MATCHES "_fragment$")
    set(stage_flag frag)
  else()
    message(FATAL_ERROR "ValidateGlslStages.cmake: cannot tell the stage of ${stage_file}")
  endif()

  # Output goes to files rather than pipes: the files are also what gets printed on failure.
  set(log_out "${stage_file}.out")
  set(log_err "${stage_file}.err")
  execute_process(
    COMMAND "${GLSLANG}" "${stage_file}" -S ${stage_flag}
    RESULT_VARIABLE result
    OUTPUT_FILE "${log_out}"
    ERROR_FILE "${log_err}"
    TIMEOUT 300)

  if(NOT result EQUAL 0)
    message("--- ${stage_flag}: ${stage_file} (exit ${result}) ---")
    file(READ "${log_out}" text_out)
    file(READ "${log_err}" text_err)
    message("${text_out}${text_err}")
    math(EXPR failed "${failed} + 1")
  endif()

  file(REMOVE "${log_out}" "${log_err}")
endforeach()

if(failed GREATER 0)
  message(FATAL_ERROR
    "${SHADER}: ${failed} stage(s) do not compile (glslangValidator). "
    "The dump is under ${STAGE_DIR} - the numbers in the messages are .shader / .glsl lines.")
endif()

# --- link -------------------------------------------------------------------------------
# Validating a stage on its own says nothing about the program it becomes. Three classes of
# error pass `-S vert` / `-S frag` and only fail when the driver links the stages:
#
#   - a function that is declared but never defined (evaluateMaterial() before the `lit` surface
#     prelude existed - the material compiled, then failed at first draw),
#   - a vertex stage that writes a varying the fragment stage declares with another type,
#   - a fragment stage that reads a varying the vertex stage never writes.
#
# filament cannot hit the first one: it links glslang into matc and its packages are SPIR-V
# modules. This is the external equivalent, one link per *distinct* program: a keyed material's
# 128 variants usually share a handful of stage texts, so the list is hashed first.
set(link_failed 0)
# A plain string, not a list: this script runs via `cmake -P`, where CMP0057 (IN_LIST) is not
# set to NEW, so `if(x IN_LIST y)` fails with "Unknown arguments".
set(link_hashes ";")
foreach(vertex_file IN LISTS stage_files)
  if(NOT vertex_file MATCHES "_vertex\\.glsl$")
    continue()
  endif()

  string(REGEX REPLACE "_vertex\\.glsl$" "" prefix "${vertex_file}")
  if(NOT EXISTS "${prefix}_fragment.glsl")
    continue()
  endif()

  file(READ "${prefix}_vertex.glsl" vertex_text)
  file(READ "${prefix}_fragment.glsl" fragment_text)
  set(geometry_text "")
  if(EXISTS "${prefix}_geometry.glsl")
    file(READ "${prefix}_geometry.glsl" geometry_text)
  endif()

  string(SHA1 program_hash "${vertex_text}${geometry_text}${fragment_text}")
  if(link_hashes MATCHES ";${program_hash};")
    continue()
  endif()
  string(APPEND link_hashes "${program_hash};")

  # glslang names the stage from the file extension in link mode, so the dumps are copied to
  # the extensions it expects (.glsl carries no stage).
  set(link_dir "${STAGE_DIR}/.link")
  file(MAKE_DIRECTORY "${link_dir}")
  file(WRITE "${link_dir}/program.vert" "${vertex_text}")
  file(WRITE "${link_dir}/program.frag" "${fragment_text}")
  set(link_files "${link_dir}/program.vert")
  if(NOT geometry_text STREQUAL "")
    file(WRITE "${link_dir}/program.geom" "${geometry_text}")
    list(APPEND link_files "${link_dir}/program.geom")
  endif()
  list(APPEND link_files "${link_dir}/program.frag")

  set(log_out "${link_dir}/link.out")
  set(log_err "${link_dir}/link.err")
  execute_process(
    COMMAND "${GLSLANG}" -l ${link_files}
    RESULT_VARIABLE result
    OUTPUT_FILE "${log_out}"
    ERROR_FILE "${log_err}"
    TIMEOUT 300)

  if(NOT result EQUAL 0)
    message("--- link: ${prefix} (exit ${result}) ---")
    file(READ "${log_out}" text_out)
    file(READ "${log_err}" text_err)
    message("${text_out}${text_err}")
    math(EXPR link_failed "${link_failed} + 1")
  endif()
endforeach()

if(link_failed GREATER 0)
  message(FATAL_ERROR
    "${SHADER}: ${link_failed} program(s) do not link (glslangValidator -l). The dump is under "
    "${STAGE_DIR} - a missing function definition or a mismatched varying shows up here and "
    "nowhere else in the build.")
endif()
