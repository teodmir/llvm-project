//===--- AssignmentDeclExistCheck.h - clang-tidy ----------------*- C++ -*-===//
// Copyright 2020 Kenneth Sterner
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MISC_ASSIGNMENTDECLEXISTCHECK_H
#define LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MISC_ASSIGNMENTDECLEXISTCHECK_H

#include "../ClangTidyCheck.h"

namespace clang {
namespace tidy {
namespace misc {

class AssignmentDeclExistCheck : public ClangTidyCheck {
private:
    std::string DeclFile;

  void checkFun(const FunctionDecl &decl);
  void checkStruct(const RecordDecl &decl, const std::string *n = NULL);
  void checkTypedef(const TypedefDecl &decl);

public:
  AssignmentDeclExistCheck(StringRef Name, ClangTidyContext *Context)
      : ClangTidyCheck(Name, Context),
        DeclFile(Options.get("DeclFile", "")) {}
  void storeOptions(ClangTidyOptions::OptionMap &Opts) override {
    Options.store(Opts, "DeclFile", DeclFile);
  }
  void registerMatchers(ast_matchers::MatchFinder *Finder) override;
  void check(const ast_matchers::MatchFinder::MatchResult &Result) override;
  void onEndOfTranslationUnit() override;
};

} // namespace misc
} // namespace tidy
} // namespace clang

#endif // LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MISC_ASSIGNMENTDECLEXISTCHECK_H
