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

#include "bolt/cudf/expression/ArrayAccessFunctions.h"
#include "bolt/cudf/expression/CommonFunctions.h"
#include "bolt/cudf/expression/ExpressionEvaluator.h"

#include "bolt/common/base/Exceptions.h"

#include <cudf/unary.hpp>

namespace bytedance::bolt::cudf_velox {

IsNullFunction::IsNullFunction(const core::TypedExprPtr& expr) {
  BOLT_CHECK_EQ(expr->inputs().size(), 1, "is_null expects 1 input");
}

ColumnOrView IsNullFunction::eval(
    std::vector<ColumnOrView>& inputColumns,
    cuda::stream_ref stream,
    rmm::device_async_resource_ref mr) const {
  BOLT_CHECK_EQ(inputColumns.size(), 1, "is_null expects 1 input");
  return cudf::is_null(asView(inputColumns[0]), stream, mr);
}

std::vector<exec::FunctionSignaturePtr> arrayAccessSignatures(
    std::initializer_list<const char*> indexTypes) {
  using exec::FunctionSignatureBuilder;

  std::vector<exec::FunctionSignaturePtr> signatures;
  signatures.reserve(indexTypes.size());
  for (const auto* indexType : indexTypes) {
    signatures.push_back(
        FunctionSignatureBuilder()
            .typeVariable("T")
            .returnType("T")
            .argumentType("array(T)")
            .argumentType(indexType)
            .build());
  }
  return signatures;
}

void registerArrayAccessFunction(
    const std::string& name,
    ArrayAccessPolicy policy,
    std::vector<exec::FunctionSignaturePtr> signatures) {
  registerCudfFunction(
      name,
      [policy](
          const std::string&,
          const core::TypedExprPtr& expr,
          memory::MemoryPool* pool,
          const core::QueryConfig&) {
        return makeArrayAccessFunction(expr, policy, pool);
      },
      signatures);
}

} // namespace bytedance::bolt::cudf_velox
