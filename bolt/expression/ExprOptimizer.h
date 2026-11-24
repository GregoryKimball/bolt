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

#include <functional>

#include "bolt/core/Expressions.h"
#include "bolt/core/QueryCtx.h"

namespace bytedance::bolt::expression {

/// Produces a replacement for a subexpression whose constant folding failed.
/// Receives the error message and the result type of the subexpression.
using MakeFailExpr = std::function<core::TypedExprPtr(
    const std::string& errorMessage,
    const TypePtr& resultType)>;

/// Constant folds 'expr' bottom up: every deterministic call or cast whose
/// inputs are all constants is evaluated and replaced by a constant. When the
/// evaluation of a subexpression throws a user error, the subexpression is
/// replaced by makeFailExpr(message, type) if 'makeFailExpr' is set, and left
/// unchanged otherwise.
core::TypedExprPtr optimize(
    const core::TypedExprPtr& expr,
    core::QueryCtx* queryCtx,
    memory::MemoryPool* pool,
    const MakeFailExpr& makeFailExpr = nullptr);

} // namespace bytedance::bolt::expression
