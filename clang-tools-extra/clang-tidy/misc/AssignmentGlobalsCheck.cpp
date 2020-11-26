//===--- AssignmentGlobalsCheck.cpp - clang-tidy --------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "AssignmentGlobalsCheck.h"
#include "clang/AST/ASTContext.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"

using namespace clang::ast_matchers;

namespace clang {
namespace tidy {
namespace misc {

void AssignmentGlobalsCheck::registerMatchers(MatchFinder *Finder) {
  Finder->addMatcher(varDecl(hasGlobalStorage()).bind("var"), this);
}

void AssignmentGlobalsCheck::check(const MatchFinder::MatchResult &Result) {
  const auto *MatchedDecl = Result.Nodes.getNodeAs<VarDecl>("var");
  diag(MatchedDecl->getLocation(), "%0 is global or static")
      << MatchedDecl;
}

} // namespace misc
} // namespace tidy
} // namespace clang
