/*
 * Copyright (c) ByteDance Ltd. and/or its affiliates.
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

// Stand-ins for Velox helpers that Velox's cuDF integration uses and Bolt does
// not have. Force-included into every bolt/cudf source.

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

#include "bolt/common/Casts.h"
#include "bolt/common/base/Exceptions.h"
#include "bolt/common/time/Timer.h"
#include "bolt/connectors/hive/HiveConnectorUtil.h"
#include "bolt/core/PlanNode.h"
#include "bolt/dwio/common/Options.h"
#include "bolt/dwio/common/Statistics.h"
#include "bolt/exec/Aggregate.h"
#include "bolt/exec/Operator.h"
#include "bolt/expression/SignatureBinder.h"
#include "bolt/functions/lib/string/StringImpl.h"

#define BOLT_NYI_CHECK(expr, ...) \
  if (!(expr)) {                  \
    BOLT_NYI(__VA_ARGS__);        \
  }

namespace bytedance::bolt {

using detail::errorMessage;
using exec::IdentityProjection;

template <typename To, typename From>
std::shared_ptr<To> checkedPointerCast(const std::shared_ptr<From>& input) {
  return checked_pointer_cast<To, From>(input);
}

template <typename To, typename From>
std::unique_ptr<To> checkedPointerCast(std::unique_ptr<From> input) {
  return checked_pointer_cast<To, From>(std::move(input));
}

template <typename To, typename From>
To* checkedPointerCast(From* input) {
  BOLT_CHECK_NOT_NULL(input);
  auto* casted = dynamic_cast<To*>(input);
  detail::ensureCastSucceeded(casted, input);
  return casted;
}

using MicrosecondWallTimer = MicrosecondTimer;

/// Reads the steady clock as an integer in 'Duration' units.
class SteadyTime {
 public:
  template <typename Duration>
  static uint64_t now() {
    return std::chrono::duration_cast<Duration>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
  }
};

namespace dwio::common {
using RuntimeStats = RuntimeStatistics;
} // namespace dwio::common

namespace connector {

/// Bolt's Hive connector has no row sampling, so 'sampleRate' is unchanged.
inline core::TypedExprPtr extractFiltersFromRemainingFilter(
    const core::TypedExprPtr& expr,
    core::ExpressionEvaluator* evaluator,
    common::SubfieldFilters& filters,
    double& /*sampleRate*/) {
  return expr == nullptr
      ? nullptr
      : hive::extractFiltersFromRemainingFilter(expr, evaluator, filters);
}

} // namespace connector

namespace exec {

/// Returns the intermediate type of aggregate 'name' for 'rawInputTypes'.
inline TypePtr resolveIntermediateType(
    const std::string& name,
    const std::vector<TypePtr>& rawInputTypes) {
  const auto signatures = getAggregateFunctionSignatures(name);
  BOLT_USER_CHECK(
      signatures.has_value(), "Aggregate function not registered: {}", name);
  for (const auto& signature : signatures.value()) {
    SignatureBinder binder(*signature, rawInputTypes);
    if (binder.tryBind()) {
      if (auto type = binder.tryResolveType(signature->intermediateType())) {
        return type;
      }
    }
  }
  BOLT_USER_FAIL("Aggregate function signature not supported: {}", name);
}

} // namespace exec

namespace functions::stringImpl {

inline std::string utf8StrToLowerCopy(const std::string& str) {
  std::string lowerStr;
  lower<false>(lowerStr, str);
  return lowerStr;
}

} // namespace functions::stringImpl

} // namespace bytedance::bolt

template <>
struct fmt::formatter<bytedance::bolt::dwio::common::FileFormat>
    : fmt::formatter<std::string> {
  auto format(bytedance::bolt::dwio::common::FileFormat format, format_context& ctx)
      const {
    return fmt::formatter<std::string>::format(
        bytedance::bolt::dwio::common::toString(format), ctx);
  }
};

template <>
struct fmt::formatter<bytedance::bolt::core::AggregationNode::Step>
    : fmt::formatter<std::string_view> {
  auto format(
      bytedance::bolt::core::AggregationNode::Step step,
      format_context& ctx) const {
    return fmt::formatter<std::string_view>::format(
        bytedance::bolt::core::AggregationNode::stepName(step), ctx);
  }
};
