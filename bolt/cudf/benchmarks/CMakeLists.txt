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

add_executable(bolt_cudf_tpch_benchmark CudfTpchBenchmark.cpp)

target_link_libraries(
  bolt_cudf_tpch_benchmark
  PRIVATE
    bolt_tpch_benchmark_lib
    bolt_query_benchmark
    bolt_testutils
    cudf::cudf
    ${FOLLY_BENCHMARK}
)
bolt_cudf_target_options(bolt_cudf_tpch_benchmark)
