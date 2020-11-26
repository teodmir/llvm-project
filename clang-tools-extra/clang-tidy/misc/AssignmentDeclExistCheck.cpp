//===--- AssignmentDeclExistCheck.cpp - clang-tidy ------------------------===//
// Copyright 2020 Kenneth Sterner
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <sstream>
#include <map>
#include "AssignmentDeclExistCheck.h"
#include "clang/AST/ASTContext.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"

using namespace clang::ast_matchers;

namespace clang {
namespace tidy {
namespace misc {

// Map from type names to the amount of occurrences in the parameter.
typedef std::map<std::string, int> ParamMap;

struct FuncDecl {
  ParamMap pMap;
  std::string retType;
};

struct Declarations {
    std::map<std::string, FuncDecl> functions;
    std::map<std::string, ParamMap> structs;
};

Declarations decls;

std::string paramString(const ParamMap &pMap) {
    std::stringstream ss;
    if (pMap.empty())
        return "no args";

    for (auto it = pMap.begin(); it != pMap.end(); ++it) {
      ss << it->second << " " << it->first << "(s)";
      if (!(std::next(it) == pMap.end()))
          ss << ", ";
    }
    return ss.str();
}

// Taken from: /llvm/lib/Analysis/DevelopmentModeInlineAdvisor.cpp (line 495)
llvm::json::Value readJsonFile(StringRef FileName) {
    auto fileBuf = llvm::MemoryBuffer::getFile(FileName);
    if (!fileBuf) {
      llvm::errs() << "Error opening json file: " << FileName << ": "
                   << fileBuf.getError().message() << "\n";
      exit(EXIT_FAILURE);
    }
    auto json = llvm::json::parse(fileBuf.get()->getBuffer());
    if (!json) {
      llvm::errs() << "Unable to parse json file " << FileName << "\n";
      exit(EXIT_FAILURE);
    }
    return json.get();
}

// Overloads fromJSON to work with FuncDecl
bool fromJSON(const llvm::json::Value &E, FuncDecl &out, llvm::json::Path P) {
  auto jsonObj = E.getAsObject();
  if (!jsonObj) {
    P.report("expected object");
    return false;
  }
  auto params = jsonObj->get("params");
  auto ret = jsonObj->get("return");
  if (!params) {
      P.report("expected property 'params'");
      return false;
  }
  if (!ret) {
      P.report("expected property 'return'");
      return false;
  }

  if (!fromJSON(*params, out.pMap, P))
    return false;
  if (!fromJSON(*ret, out.retType, P))
    return false;

  return true;
}

// Overloads fromJSON to work with Declarations
bool fromJSON(const llvm::json::Value &E, Declarations &out,
              llvm::json::Path P) {
  auto jsonObj = E.getAsObject();
  if (!jsonObj) {
    P.report("expected object");
    return false;
  }
  auto funcs = jsonObj->get("functions");
  auto structs = jsonObj->get("structs");
  // The absence of functions/structs is allowed.
  if (funcs && !fromJSON(*funcs, out.functions, P))
    return false;
  if (structs && !fromJSON(*structs, out.structs, P))
    return false;

  return true;
}

// TODO: read json file location from options
void AssignmentDeclExistCheck::registerMatchers(MatchFinder *Finder) {
  if (DeclFile.empty())
      return;

  auto json = readJsonFile(DeclFile);
  llvm::json::Path::Root root("Declarations");
  if (!fromJSON(json, decls, llvm::json::Path(root))) {
    llvm::errs() << "Declarations: " << root.getError() << "\n";
    return;
    }
    Finder->addMatcher(functionDecl().bind("fun"), this);
    Finder->addMatcher(recordDecl().bind("struct"), this);
    Finder->addMatcher(typedefDecl().bind("typedefStruct"), this);
}

// Given an iterator range of comparable elements, return a map from
// the elements to the amount of times they occur in the range.
template<typename Iter>
std::map<typename std::iterator_traits<Iter>::value_type, int>
countOccurrences(Iter begin, Iter end)
{
    std::map<typename std::iterator_traits<Iter>::value_type, int> m;
    for (auto it = begin; it != end; ++it)
        m[*it]++;
    return m;
}

void AssignmentDeclExistCheck::onEndOfTranslationUnit() {
  if (!decls.functions.empty()) {
    llvm::errs() << "MISSING FUNCTION(s):\n";
    for (const auto &it : decls.functions)
      llvm::errs() << it.first << "\n";
  }
  if (!decls.structs.empty()) {
    llvm::errs() << "MISSING STRUCT(s):\n";
    for (const auto &it : decls.structs)
      llvm::errs() << it.first << "\n";
  }
}

// Remove the prefix keyword "struct" from a string that represents a
// type (Clang doesn't seem to support this natively).
std::string removeStructPrefix(const std::string &s) {
    std::string target = "struct ";
    return (s.rfind(target, 0) == 0) ? s.substr(target.length()) : s;
}

std::string cleanTypeString(QualType t) {
    return removeStructPrefix(t.getUnqualifiedType().getAsString());
}

void AssignmentDeclExistCheck::checkFun(const FunctionDecl &decl) {
  auto name = decl.getNameAsString();
  auto it = decls.functions.find(name);

  if (it == decls.functions.end())
    return;

  // Do the return type checking first, and continue with the
  // parameter list if necessary.
  auto retType = cleanTypeString(decl.getReturnType());
  auto info = it->second;
  auto expectedRet = info.retType;
  if (retType != expectedRet) {
    diag(decl.getLocation(),
         "Return value of '%0' should be of type '%1', but is of type '%2'")
        << name << expectedRet << retType;
    decls.functions.erase(it);
    return;
  }
  std::vector<std::string> typeStrings;
  for (const auto &it : decl.parameters())
      typeStrings.push_back(cleanTypeString(it->getType()));
  if (info.pMap != countOccurrences(typeStrings.begin(), typeStrings.end()))
      diag(decl.getLocation(),
           "Wrong parameters of '%0': should consist of %1")
          << name << paramString(info.pMap);
  decls.functions.erase(it);
}

// Optional name parameter is for typedefs, since they should be
// referred to with the typedef identifier rather than the underlying
// struct name (since the latter can be anonymous as well).
void AssignmentDeclExistCheck::checkStruct(const RecordDecl &decl,
                                           const std::string *n) {
  auto name = n ? *n : decl.getNameAsString();
  auto it = decls.structs.find(name);
  if (it == decls.structs.end())
    return;

  auto pMap = it->second;
  std::vector<std::string> typeStrings;
  for (const auto &it : decl.fields())
    typeStrings.push_back(cleanTypeString(it->getType()));
  if (pMap != countOccurrences(typeStrings.begin(), typeStrings.end()))
    diag(decl.getLocation(), "Wrong members of '%0': should consist of %1")
        << name << paramString(pMap);
  decls.structs.erase(it);
}

void AssignmentDeclExistCheck::checkTypedef(const TypedefDecl &decl) {
    auto name = std::string(decl.getNameAsString());
    auto d = decl.getUnderlyingType()->getAsRecordDecl();
    if (!d)
      return;
    checkStruct(*d, &name);
}

void AssignmentDeclExistCheck::check(const MatchFinder::MatchResult &Result) {
  const auto *MatchedFun = Result.Nodes.getNodeAs<FunctionDecl>("fun");
  const auto *MatchedStruct = Result.Nodes.getNodeAs<RecordDecl>("struct");
  const auto *MatchedTypedef = Result.Nodes.getNodeAs<TypedefDecl>("typedefStruct");

  if (MatchedFun)
    checkFun(*MatchedFun);
  else if (MatchedStruct)
    checkStruct(*MatchedStruct);
  else if (MatchedTypedef)
    checkTypedef(*MatchedTypedef);
}

} // namespace misc
} // namespace tidy
} // namespace clang
