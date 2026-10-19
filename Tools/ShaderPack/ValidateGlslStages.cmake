# Validates the GLSL MoonMatc just generated, stage by stage, with an external glslangValidator.
#
# Run in script mode, from the MoonMatc custom command (see CMakeLists.txt in this directory):
#
#   cmake -DGLSLANG=<glslangValidator> -DSTAGE_DIR=<dir> -DSHADER=<name.mat> -P ValidateGlslStages.cmake
#
# This is the build-time half that filament's matc gets for free by linking glslang: a shader
# that does not compile stops the build instead of failing at run time when the material is
# first loaded. MoonMatc itself stays a text tool - it emits every (pass, variant, stage) with
# `--dump-glsl` and this script hands each one to the driver-agnostic validator.
#
# The dumped stage names end in _vertex / _fragment / _geometry, and the file's own #line
# directives (plus the GL_GOOGLE_cpp_style_line_directive extension MoonMatc emits) make any
# error point back at the .mat / .glsl line that caused it.

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
    "The dump is under ${STAGE_DIR} - the numbers in the messages are .mat / .glsl lines.")
endif()
