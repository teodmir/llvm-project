//===--- AssignmentGotoCheck.cpp - clang-tidy -----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "AssignmentGotoCheck.h"
#include "clang/AST/ASTContext.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"

using namespace clang::ast_matchers;

namespace clang {
namespace tidy {
namespace misc {

void AssignmentGotoCheck::registerMatchers(MatchFinder *Finder) {
  Finder->addMatcher(gotoStmt().bind("goto"), this);
}

void AssignmentGotoCheck::check(const MatchFinder::MatchResult &Result) {
  const auto *Goto = Result.Nodes.getNodeAs<GotoStmt>("goto");
  diag(Goto->getGotoLoc(), "Goto statements should be avoided")
      << Goto->getSourceRange();
}

} // namespace misc
} // namespace tidy
} // namespace clang
