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

bolt_cudf_library(
  bolt_cudf_exec
  AggregationRegistry.cpp
  CudfAggregation.cpp
  CudfAssignUniqueId.cpp
  CudfBatchConcat.cpp
  CudfConversion.cpp
  CudfDistinct.cpp
  CudfEnforceSingleRow.cpp
  CudfFilterProject.cpp
  CudfGroupId.cpp
  CudfGroupby.cpp
  CudfHashJoin.cpp
  CudfJoin.cpp
  CudfJoinBuild.cpp
  CudfLimit.cpp
  CudfLocalMerge.cpp
  CudfLocalPartition.cpp
  CudfMarkDistinct.cpp
  CudfNestedLoopJoin.cpp
  CudfOrderBy.cpp
  CudfReduce.cpp
  CudfTopN.cpp
  CudfTopNRowNumber.cpp
  CudfWindow.cpp
  DebugUtil.cpp
  DecimalAggregationDevice.cu
  DecimalAggregationHostOps.cpp
  DecimalAggregationState.cpp
  GpuCapabilities.cpp
  GpuResources.cpp
  OperatorAdapters.cpp
  PrestoAggregateFunctions.cpp
  SparkAggregateFunctions.cpp
  ToCudf.cpp
  Utilities.cpp
  VeloxCudfInterop.cpp
)
