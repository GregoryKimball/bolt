/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include "bolt/cudf/CudfConfig.h"
#include "bolt/cudf/vector/CudfVector.h"

#include "bolt/common/base/Exceptions.h"

#include <cuda_runtime.h>

namespace bytedance::bolt::cudf_velox {

/// Helper function to check for CUDA errors in debug mode.
inline void checkCudaErrorInDebug() {
  if (CudfConfig::getInstance().debugEnabled) {
    cudaError_t err = cudaGetLastError();
    BOLT_CHECK(
        err == cudaSuccess, "CUDA error detected: {}", cudaGetErrorString(err));
  }
}

class DebugUtil {
 public:
  std::string toString(
      const cudf::table_view& table,
      cuda::stream_ref stream,
      vector_size_t from,
      vector_size_t to);

 private:
  std::shared_ptr<memory::MemoryPool> rootPool_{
      memory::memoryManager()->addRootPool()};
  std::shared_ptr<memory::MemoryPool> pool_{
      rootPool_->addLeafChild("debug_util")};
};

} // namespace bytedance::bolt::cudf_velox
