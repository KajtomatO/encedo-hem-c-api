# examples/ — the SDK's runnable examples (REQ-API-007): public-API consumers
# compiled with the SDK whenever EHEM_BUILD_EXAMPLES is on (the default in a
# top-level build, so every CI leg compiles them), never run by the build —
# they need a device. docs/API-GUIDE.md "Examples and hem-tool" explains them.
# Until the SDK's 1.2.1 hem-tool, then in this repository, was the runnable
# example; the full-featured client is encedo-hem-tool's since.
#
# implements: REQ-API-007
#
# Each example links the shared SDK when the build has it, else the static one
# — the two ways a consumer links — and nothing of the test tree.
function(ehem_add_example name)
  add_executable(ehem-example-${name} ${name}.c)
  if(TARGET encedo-hem)
    target_link_libraries(ehem-example-${name} PRIVATE encedo-hem::encedo-hem)
  else()
    target_link_libraries(ehem-example-${name} PRIVATE encedo-hem::encedo-hem-static)
  endif()
  target_compile_options(ehem-example-${name} PRIVATE ${EHEM_WARNING_FLAGS})
  set_target_properties(ehem-example-${name} PROPERTIES OUTPUT_NAME ${name})
endfunction()

ehem_add_example(status)   # examples/status: the device's status under system trust
ehem_add_example(sign)     # examples/sign LABEL [MESSAGE]: login, key lookup, a signature
