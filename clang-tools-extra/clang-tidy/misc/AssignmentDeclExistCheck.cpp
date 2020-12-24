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
#include <vector>
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

struct TypeInfo {
  bool isVar;
  std::string name;
  int pointers;

  TypeInfo() : isVar{false}, name{""}, pointers{0} {}
  TypeInfo(bool isVar, const std::string &name, int pointers)
      : isVar{isVar}, name{name}, pointers{pointers} {}

  std::string toString() {
      std::stringstream ret;
      if (isVar)
        ret << '%';
      ret << name;
      if (pointers > 0) {
        ret << ' ';
        for (int i = 0; i < pointers; i++)
          ret << '*';
      }
      return ret.str();
  }

  void debugPrint() { llvm::errs() << toString() << "\n"; }

  bool operator ==(const TypeInfo &rhs) {
    return isVar == rhs.isVar && name == rhs.name && pointers == rhs.pointers;
  }

  bool operator<(const TypeInfo &rhs) {
    return isVar < rhs.isVar && name < rhs.name && pointers < rhs.pointers;
  }
};

class ParseTypeError : public llvm::ErrorInfo<ParseTypeError> {
public:
  static char ID;
  std::string str;
  size_t pos;

  ParseTypeError(const std::string &str, size_t pos) : str{str}, pos{pos} {}

  void log(raw_ostream &OS) const override {
    OS << "Unexpected character: " << '\'' << str[pos] << '\'' << " in " << '"'
       << str << '"' << " (" << pos << ')';
  }

  // This is temporary.
  std::error_code convertToErrorCode() const override {
      return std::make_error_code(std::errc::no_message_available);
  }
};

char ParseTypeError::ID; // This should be declared in the C++ file.

llvm::Expected<TypeInfo> parseType(const std::string &s) {
  size_t pos = 0;
  TypeInfo ret;
  auto structPrefix = "struct ";
  auto structPrefixLen = strlen(structPrefix);

  if (s[pos] == '%') {
    ret.isVar = true;
    pos++;
  }
  // Skip structs
  if (s.substr(pos, strlen(structPrefix)) == structPrefix) {
    pos += structPrefixLen;
    // Skip whitespace
    while (s[pos] == ' ')
      pos++;
  }

  if (!(isalpha(s[pos]) || s[pos] == '_'))
      return llvm::make_error<ParseTypeError>(s, pos);

  std::stringstream identifier;
  identifier << s[pos];
  pos++;

  while (isAlphanumeric(s[pos]) || s[pos] == '_') {
    identifier << s[pos];
    pos++;
  }
  ret.name = identifier.str();

  // Already at end (no pointer asterisks), so return early.
  if (pos == s.length())
      return ret;

  while (s[pos] == ' ')
    pos++;

  if (pos == s.length())
    return llvm::make_error<ParseTypeError>(s, pos);

  // clean this up
  while (pos < s.length()) {
    if (s[pos] == '*') {
      ret.pointers++;
      pos++;
    } else {
      return llvm::make_error<ParseTypeError>(s, pos);
    }
  }

  return ret;
}

struct FuncDecl {
  ParamMap pMap;
  std::string retType;

public:
  FuncDecl() {}
  FuncDecl(const ParamMap &p, const std::string &ret) : pMap(p), retType(ret) {}

  bool operator ==(const FuncDecl &rhs) const {
    return pMap == rhs.pMap && retType == rhs.retType;
  }

  bool operator !=(const FuncDecl &rhs) const {
      return !operator ==(rhs);
  }

  std::string pretty() const {
    std::stringstream ss;
    ss << "(";
    for (auto it = pMap.begin(); it != pMap.end(); ++it) {
      ss << it->first << ": " << it->second;
      if (!(std::next(it) == pMap.end()))
        ss << ", ";
    }
    ss << ") -> " << retType;
    return ss.str();
  }
};

std::string prettyMapAsStruct(const ParamMap &pMap) {
    std::stringstream ss;
    ss << "{ ";
    for (auto it = pMap.begin(); it != pMap.end(); ++it) {
      ss << it->first << ": " << it->second << "; ";
    }
    ss << "};";
    return ss.str();
}

struct Declarations {
    std::map<std::string, FuncDecl> functions;
    std::map<std::string, ParamMap> structs;
    std::vector<FuncDecl> unnamedFunctions;
    std::vector<ParamMap> unnamedStructs;
    std::map<std::string, ParamMap> varStructs;
};

Declarations decls;

std::map<std::string, FuncDecl> foundFuncs;
std::map<std::string, SourceLocation> funcLocMap;
std::map<std::string, ParamMap> foundStructs;
std::map<std::string, SourceLocation> structLocMap;

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
  auto unnamedFuncs = jsonObj->get("functions*");
  auto unnamedStructs = jsonObj->get("structs*");
  auto varStructs = jsonObj->get("%structs");
  // The absence of functions/structs is allowed.
  if (funcs && !fromJSON(*funcs, out.functions, P))
    return false;
  if (structs && !fromJSON(*structs, out.structs, P))
    return false;
  if (unnamedFuncs && !fromJSON(*unnamedFuncs, out.unnamedFunctions, P))
      return false;
  if (unnamedStructs && !fromJSON(*unnamedStructs, out.unnamedStructs, P))
      return false;
  if (varStructs && !fromJSON(*varStructs, out.varStructs, P))
      return false;

  return true;
}

// Check if any anonymous declarations overlaps with any named declarations.
void checkOverlappingDefinitions() {
  std::vector<FuncDecl> funcs;
  for (const auto &it : decls.functions)
    funcs.push_back(it.second);
  std::vector<ParamMap> structs;
  for (const auto &it : decls.structs)
      structs.push_back(it.second);

  for (const auto &it : decls.unnamedFunctions)
    if (std::count(funcs.begin(), funcs.end(), it)) {
      llvm::errs() << "Unnamed function declaration " << it.pretty()
                   << " has a named counterpart\n";
      exit(EXIT_FAILURE);
    }

  for (const auto &it : decls.unnamedStructs)
    if (std::count(structs.begin(), structs.end(), it)) {
      llvm::errs() << "Unnamed struct declaration " << prettyMapAsStruct(it)
                   << " has a named counterpart\n";
      exit(EXIT_FAILURE);
    }
}

void AssignmentDeclExistCheck::registerMatchers(MatchFinder *Finder) {
  if (DeclFile.empty())
      return;

  auto json = readJsonFile(DeclFile);
  llvm::json::Path::Root root("Declarations");
  if (!fromJSON(json, decls, llvm::json::Path(root))) {
    llvm::errs() << "Declarations: " << root.getError() << "\n";
    return;
  }
  // checkOverlappingDefinitions();
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

void associateVar(const std::string &var, const std::string &target,
                  std::map<std::string, std::string> &varMap) {
  auto maybeType = parseType(var);
  if (maybeType) {
    if (maybeType->isVar) {
      llvm::errs() << "Redundant variable '%'"
                   << " in " << var << ", skipped\n";
      return;
    }
    if (maybeType->pointers > 0) {
      llvm::errs() << "Unexpected pointer asterisks in " << var
                   << ", skipped\n";
      return;
    }
    varMap.insert(std::make_pair(maybeType->name, target));
  } else {
      llvm::errs() << maybeType.takeError() << ", skipped\n";
      return;
  }
}

// Look up var in varMap, if it is a variable, otherwise return it as is.
llvm::Expected<std::string>
resolveVar(const std::string &var,
           const std::map<std::string, std::string> &varMap) {
  if (auto maybeType = parseType(var)) {
    if (!maybeType->isVar)
      return maybeType->name;
    auto resolved = varMap.find(maybeType->name);
    if (resolved == varMap.end()) {
      std::stringstream ss;
      ss << "No such variable: " << maybeType->name;
      return llvm::createStringError(std::errc::bad_message, ss.str().c_str());
    }
    // Ensure pointers are preserved after resolving
    return TypeInfo(false, resolved->second, maybeType->pointers).toString();
  } else {
    return maybeType.takeError();
  }
}

llvm::Expected<ParamMap>
resolveParams(const ParamMap &params,
           const std::map<std::string, std::string> &varMap) {
  ParamMap ret;
  for (const auto &it : params)
    if (auto maybeType = parseType(it.first)) {
        if (auto var = resolveVar(it.first, varMap))
            ret.insert(std::make_pair(*var, it.second));
        else
            return maybeType.takeError();
    } else {
      return maybeType.takeError();
    }
  return ret;
}

llvm::Expected<FuncDecl>
resolveFunction(const FuncDecl &params,
                const std::map<std::string, std::string> &varMap) {
    FuncDecl ret;
    if (auto maybeParams = resolveParams(params.pMap, varMap))
        ret.pMap = *maybeParams;
    else
        return maybeParams.takeError();

    if (auto maybeRet = resolveVar(params.retType, varMap))
        ret.retType = *maybeRet;
    else
        return maybeRet.takeError();
    return ret;
}

void AssignmentDeclExistCheck::onEndOfTranslationUnit() {
  // Bind variable names to structs that match.
  std::map<std::string, std::string> varMap;
  for (const auto &var : decls.varStructs)
    for (const auto &found : foundStructs)
      if (var.second == found.second)
        associateVar(var.first, found.first, varMap);

  std::map<std::string, FuncDecl> funcs;
  for (const auto &func : decls.functions) {
    auto name = func.first;
    if (auto maybeFunc = resolveFunction(func.second, varMap))
      funcs.insert(std::make_pair(name, *maybeFunc));
    else
      llvm::errs() << maybeFunc.takeError() << ", skipped " << name << "\n";
  }

  std::map<std::string, ParamMap> structs;
  for (const auto &param : decls.structs) {
    auto name = param.first;
    if (auto maybeParam = resolveParams(param.second, varMap))
      structs.insert(std::make_pair(name, *maybeParam));
    else
      llvm::errs() << maybeParam.takeError() << " skipped " << name << "\n";
  }

  std::vector<FuncDecl> unnamedFuncs;
  for (const auto &func : decls.unnamedFunctions) {
    if (auto maybeFunc = resolveFunction(func, varMap))
      unnamedFuncs.push_back(*maybeFunc);
    else
      llvm::errs() << maybeFunc.takeError() << " skipped " << func.pretty()
                   << "\n";
  }

  std::vector<ParamMap> unnamedStructs;
  for (const auto &rec : decls.unnamedStructs) {
    if (auto maybeRec = resolveParams(rec, varMap))
      unnamedStructs.push_back(*maybeRec);
    else
      llvm::errs() << maybeRec.takeError() << " skipped "
                   << prettyMapAsStruct(rec) << "\n";
  }

  std::map<std::string, ParamMap> varStructs;
  for (const auto &param : decls.varStructs) {
    auto name = param.first;
    if (auto maybeParam = resolveParams(param.second, varMap))
      varStructs.insert(std::make_pair(name, *maybeParam));
    else
      llvm::errs() << maybeParam.takeError() << " skipped " << name << "\n";
  }

  // Search for named first, fall back to unnamed
  for (const auto &func : foundFuncs) {
    auto name = func.first;
    auto maybeNamedFunc = funcs.find(name);
    if (maybeNamedFunc != funcs.end()) {
      if (func.second != maybeNamedFunc->second)
        diag(funcLocMap.find(name)->second, "Expected %0 but got %1")
            << maybeNamedFunc->second.pretty() << func.second.pretty();
      funcs.erase(maybeNamedFunc);
    } else {
      auto maybeUnnamedFunc = std::find(unnamedFuncs.begin(),
                                        unnamedFuncs.end(), func.second);
      if (maybeUnnamedFunc != unnamedFuncs.end())
          unnamedFuncs.erase(maybeUnnamedFunc);
    }
  }

  // Same for structs, but varStructs are considered as well
  for (const auto &record : foundStructs) {
    auto name = record.first;
    auto maybeNamedRec = structs.find(name);
    auto maybeVarRec =
        std::find_if(varStructs.begin(), varStructs.end(),
                     [&record](const std::pair<std::string, ParamMap> &pm) {
                       return pm.second == record.second;
                     });
    if (maybeNamedRec != structs.end()) {
      if (record.second != maybeNamedRec->second)
        diag(funcLocMap.find(name)->second, "Expected %0 but got %1")
            << prettyMapAsStruct(maybeNamedRec->second)
            << prettyMapAsStruct(record.second);
      structs.erase(maybeNamedRec);
    } else if (maybeVarRec != varStructs.end()) {
      varStructs.erase(maybeVarRec);
    } else {
      auto maybeUnnamedStruct = std::find(unnamedStructs.begin(),
                                          unnamedStructs.end(), record.second);
      if (maybeUnnamedStruct != unnamedStructs.end())
        unnamedStructs.erase(maybeUnnamedStruct);
    }
  }

  if (!funcs.empty()) {
    llvm::errs() << "MISSING NAMED FUNCTION(s):\n";
    for (const auto &it : funcs)
      llvm::errs() << it.first << "\n";
  }
  if (!structs.empty()) {
    llvm::errs() << "MISSING NAMED STRUCT(s):\n";
    for (const auto &it : structs)
      llvm::errs() << it.first << "\n";
  }
  if (!unnamedFuncs.empty()) {
    llvm::errs() << "MISSING UNNAMED FUNCTION(s):\n";
    for (const auto &it : unnamedFuncs)
      llvm::errs() << it.pretty() << "\n";
  }
  if (!unnamedStructs.empty()) {
    llvm::errs() << "MISSING UNNAMED STRUCT(s):\n";
    for (const auto &it : unnamedStructs)
      llvm::errs() << prettyMapAsStruct(it) << "\n";
  }
  if (!varStructs.empty()) {
    llvm::errs() << "MISSING VARIABLE STRUCT(s):\n";
    for (const auto &it : varStructs)
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
  // llvm::errs() << "func\n";
  auto name = decl.getNameAsString();
  if (name == "main")
      return;

  std::vector<std::string> typeStrings;
  for (const auto &it : decl.parameters())
      typeStrings.push_back(cleanTypeString(it->getType()));

  auto retType = cleanTypeString(decl.getReturnType());
  auto currentFunc = FuncDecl(countOccurrences(typeStrings.begin(),
                                               typeStrings.end()),
                              retType);
  foundFuncs.insert(std::make_pair(name, currentFunc));
  funcLocMap.insert(std::make_pair(name, decl.getLocation()));
}

// Optional name parameter is for typedefs, since they should be
// referred to with the typedef identifier rather than the underlying
// struct name (since the latter can be anonymous as well).
void AssignmentDeclExistCheck::checkStruct(const RecordDecl &decl,
                                           const std::string *n) {
  auto name = n ? *n : decl.getNameAsString();
  std::vector<std::string> typeStrings;
  for (const auto &it : decl.fields())
    typeStrings.push_back(cleanTypeString(it->getType()));
  auto currentStruct = countOccurrences(typeStrings.begin(), typeStrings.end());
  foundStructs.insert(make_pair(name, currentStruct));
  structLocMap.insert(std::make_pair(name, decl.getLocation()));
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
