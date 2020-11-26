//===--- AssignmentNocppCheck.cpp - clang-tidy ------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "AssignmentNoCppCheck.h"
#include "clang/AST/ASTContext.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"

using namespace clang::ast_matchers;

namespace clang {
namespace tidy {
namespace misc {

void AssignmentNoCppCheck::onStartOfTranslationUnit() {
  if (getLangOpts().CPlusPlus) {
      llvm::errs() << "*** C++ features are not allowed ***\n";
  }
}

} // namespace misc
} // namespace tidy
} // namespace clang
