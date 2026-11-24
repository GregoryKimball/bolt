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

#include "bolt/expression/ExprOptimizer.h"

#include "bolt/expression/Expr.h"
#include "bolt/vector/ComplexVector.h"

namespace bytedance::bolt::expression {
namespace {

core::TypedExprPtr withInputs(
    const core::TypedExprPtr& expr,
    std::vector<core::TypedExprPtr> inputs) {
  if (expr->isCallKind()) {
    return std::make_shared<core::CallTypedExpr>(
        expr->type(),
        std::move(inputs),
        expr->asUnchecked<core::CallTypedExpr>()->name());
  }
  return std::make_shared<core::CastTypedExpr>(
      expr->type(),
      inputs,
      expr->asUnchecked<core::CastTypedExpr>()->nullOnFailure());
}

// Evaluates 'expr', whose inputs are all constants, on a single row. Returns
// nullptr if 'expr' is not deterministic.
core::TypedExprPtr evaluate(
    const core::TypedExprPtr& expr,
    core::QueryCtx* queryCtx,
    memory::MemoryPool* pool) {
  core::ExecCtx execCtx(pool, queryCtx);
  exec::ExprSet exprSet({expr}, &execCtx);
  if (!exprSet.exprs()[0]->isDeterministic()) {
    return nullptr;
  }
  RowVector input(pool, ROW({}, {}), nullptr, 1, std::vector<VectorPtr>{});
  SelectivityVector rows(1);
  exec::EvalCtx evalCtx(&execCtx, &exprSet, &input);
  std::vector<VectorPtr> results(1);
  exprSet.eval(rows, evalCtx, results);
  return std::make_shared<core::ConstantTypedExpr>(
      BaseVector::wrapInConstant(1, 0, results[0]));
}

} // namespace

core::TypedExprPtr optimize(
    const core::TypedExprPtr& expr,
    core::QueryCtx* queryCtx,
    memory::MemoryPool* pool,
    const MakeFailExpr& makeFailExpr) {
  if (expr == nullptr || !(expr->isCallKind() || expr->isCastKind())) {
    return expr;
  }
  std::vector<core::TypedExprPtr> inputs;
  inputs.reserve(expr->inputs().size());
  bool changed = false;
  bool allConstant = true;
  for (const auto& input : expr->inputs()) {
    auto optimized = optimize(input, queryCtx, pool, makeFailExpr);
    changed |= optimized != input;
    allConstant &= optimized->isConstantKind();
    inputs.push_back(std::move(optimized));
  }
  auto current = changed ? withInputs(expr, std::move(inputs)) : expr;
  if (!allConstant) {
    return current;
  }
  try {
    auto folded = evaluate(current, queryCtx, pool);
    return folded ? folded : current;
  } catch (const BoltUserError& e) {
    return makeFailExpr ? makeFailExpr(e.message(), current->type()) : current;
  }
}

} // namespace bytedance::bolt::expression
