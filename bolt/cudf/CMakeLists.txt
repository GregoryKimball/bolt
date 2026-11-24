#
# Copyright (c) ByteDance Ltd. and/or its affiliates
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# Velox's cuDF integration, imported by scripts/import-velox-cudf.py. Each directory is an object
# library folded into bolt_engine.
set(BOLT_CUDF_COMPAT_HEADER ${CMAKE_CURRENT_SOURCE_DIR}/compat/VeloxCompat.h)

# Applies the settings every bolt/cudf C++ target needs.
function(bolt_cudf_target_options target_name)
  target_compile_options(
    ${target_name}
    PRIVATE
      $<$<COMPILE_LANGUAGE:CXX>:-Wno-missing-field-initializers>
      "$<$<COMPILE_LANGUAGE:CXX>:SHELL:-include ${BOLT_CUDF_COMPAT_HEADER}>"
  )
endfunction()

function(bolt_cudf_library target_name)
  bolt_add_library(${target_name} ${ARGN})
  bolt_cudf_target_options(${target_name})
  target_link_libraries(
    ${target_name}
    PUBLIC
      bolt_exec
      bolt_expression
      bolt_hive_connector
      bolt_vector
      cudf::cudf
      nvtx3::nvtx3-cpp
      arrow::arrow
      Folly::folly
      fmt::fmt
  )
  # Enables the experimental <cuda/memory_resource> header used by RMM and libcudf.
  target_compile_definitions(
    ${target_name}
    PUBLIC
      LIBCUDACXX_ENABLE_EXPERIMENTAL_MEMORY_RESOURCE
  )
endfunction()

add_subdirectory(vector)
add_subdirectory(expression)
add_subdirectory(connectors/hive)
add_subdirectory(exec)

if(${BOLT_BUILD_BENCHMARKS})
  add_subdirectory(benchmarks)
endif()
