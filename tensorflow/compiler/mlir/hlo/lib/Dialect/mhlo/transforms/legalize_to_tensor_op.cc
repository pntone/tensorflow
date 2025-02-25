/* Copyright 2021 The TensorFlow Authors. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/

// This file implements logic for lowering bufferization.to_tensor ops that are
// inserted during `mhlo-legalize-to-lmhlo`.

#include "mlir-hlo/Dialect/mhlo/transforms/PassDetail.h"
#include "mlir-hlo/Dialect/mhlo/transforms/passes.h"
#include "mlir/Dialect/Bufferization/IR/Bufferization.h"
#include "mlir/Dialect/MemRef/IR/MemRef.h"
#include "mlir/Dialect/Shape/IR/Shape.h"
#include "mlir/Dialect/StandardOps/IR/Ops.h"
#include "mlir/Dialect/Tensor/IR/Tensor.h"  // TF:llvm-project
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/BuiltinTypes.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Pass/PassRegistry.h"
#include "mlir/Support/LogicalResult.h"
#include "mlir/Transforms/GreedyPatternRewriteDriver.h"

namespace mlir {
namespace lmhlo {
namespace {
using shape::ShapeOfOp;
using tensor::ExtractOp;

// Converting:
//   memref (operand) -> bufferization.to_tensor -> tensor.extract
//     to
//   memref (operand) -> memref.load
struct ForwardExtractOp : public OpRewritePattern<ExtractOp> {
  using OpRewritePattern<ExtractOp>::OpRewritePattern;

  LogicalResult matchAndRewrite(ExtractOp extract,
                                PatternRewriter& rewriter) const override {
    auto to_tensor =
        extract.tensor().getDefiningOp<bufferization::ToTensorOp>();
    if (!to_tensor) return failure();

    rewriter.replaceOpWithNewOp<memref::LoadOp>(
        extract, extract.getType(), to_tensor.memref(), extract.indices());
    return success();
  }
};

// Converting:
//   memref (operand) -> bufferization.to_tensor -> shape.shape_of
//     to
//   memref (operand) -> shape.shape_of
struct ForwardShapeOfOp : public OpRewritePattern<ShapeOfOp> {
  using OpRewritePattern<ShapeOfOp>::OpRewritePattern;

  LogicalResult matchAndRewrite(ShapeOfOp shape_of,
                                PatternRewriter& rewriter) const override {
    auto to_tensor =
        shape_of.getArg().getDefiningOp<bufferization::ToTensorOp>();
    if (!to_tensor) return failure();

    rewriter.replaceOpWithNewOp<ShapeOfOp>(shape_of, shape_of.getType(),
                                           to_tensor.memref());
    return success();
  }
};

struct LegalizeToTensorOpPass
    : public LegalizeToTensorOpPassBase<LegalizeToTensorOpPass> {
  // Perform the lowering to remove bufferization.to_tensor ops inserted during
  // `mhlo-legalize-to-lmhlo`.
  void runOnFunction() override {
    auto func = getFunction();
    auto context = &getContext();
    OwningRewritePatternList patterns(context);
    patterns.insert<ForwardShapeOfOp, ForwardExtractOp>(context);
    if (failed(applyPatternsAndFoldGreedily(func, std::move(patterns)))) {
      func.emitError("applyPatternsAndFoldGreedily does not converge");
      signalPassFailure();
    }
  }
};

}  // namespace

}  // namespace lmhlo
}  // namespace mlir

std::unique_ptr<mlir::OperationPass<mlir::FuncOp>>
mlir::lmhlo::createLegalizeToTensorOpPass() {
  return std::make_unique<LegalizeToTensorOpPass>();
}
