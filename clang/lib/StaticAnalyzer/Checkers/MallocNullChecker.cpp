//===-- MallocNullChecker.cpp -----------------------------------------*- C++ -*--//
// Copyright 2020 Kenneth Sterner
// Based partly on SimpleStreamChecker (tracking function calls)
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Ensure pointers returned by malloc/calloc/realloc are checked for NULL.
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CallEvent.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"
#include <utility>

using namespace clang;
using namespace ento;

namespace {
class MallocNullChecker : public Checker<check::PostCall,
                                         check::Event<ImplicitNullDerefEvent>> {
  CallDescription MallocFn, CallocFn, ReallocFn;
  std::unique_ptr<BugType> MallocNullBugType;

  void reportBug(SymbolRef MemSym, const ExplodedNode *sink,
                 const SourceRange &range, BugReporter *BR) const;

public:
  MallocNullChecker();
  // malloc, calloc, realloc
  void checkPostCall(const CallEvent &Call, CheckerContext &C) const;
  // dereference
  void checkEvent(ImplicitNullDerefEvent Event) const;
  bool isPartOfAllocedRegions(const MemRegion *Mr, ProgramStateRef State) const;
  };
} // end anonymous namespace

MallocNullChecker::MallocNullChecker()
    : MallocFn("malloc", 1), CallocFn("calloc", 2), ReallocFn("realloc", 2) {
  MallocNullBugType.reset(
      new BugType(this, "Dynamic memory is possibly NULL", "malloc Error"));
}

// List of SVals referring to allocation return values.
REGISTER_LIST_WITH_PROGRAMSTATE(AllocedRegions, SVal)

bool MallocNullChecker::isPartOfAllocedRegions(const MemRegion *Mr,
                                               ProgramStateRef State) const {
    auto list = State->get<AllocedRegions>();
    // SymbolicBase refers to the start of the memory region (or so I
    // believe, this was mostly a lucky guess).
    auto derefReg = Mr->getSymbolicBase();
    for (auto it : list) {
      auto allocReg = it.getAsRegion()->getSymbolicBase();
      if (derefReg->getSymbol() == allocReg->getSymbol())
        return true;
    }
    return false;
}

void MallocNullChecker::checkPostCall(const CallEvent &Call,
                                      CheckerContext &C) const {
  if (!Call.isGlobalCFunction())
    return;

  if (!(Call.isCalled(MallocFn) || Call.isCalled(CallocFn) ||
        Call.isCalled(ReallocFn)))
    return;

  auto state = C.getState();
  SVal allocRet = Call.getReturnValue();

  ProgramStateRef State = C.getState();
  State = State->add<AllocedRegions>(allocRet);
  C.addTransition(State);
}

void MallocNullChecker::checkEvent(ImplicitNullDerefEvent Event) const {
    auto state = Event.SinkNode->getState();
    auto derefedSVal = Event.Location;
    auto derefedRegion = derefedSVal.getAsRegion();

    if (isPartOfAllocedRegions(derefedRegion, state))
        reportBug(derefedSVal.getAsSymbol(), Event.SinkNode,
                  derefedRegion->sourceRange(), Event.BR);
}

void MallocNullChecker::reportBug(SymbolRef MemSym,
                                  const ExplodedNode *sink,
                                  const SourceRange &range,
                                  BugReporter *BR) const {
  auto R = std::make_unique<PathSensitiveBugReport>(
      *MallocNullBugType, "Usage of possibly NULL memory", sink);
  R->addRange(range);
  R->markInteresting(MemSym);
  BR->emitReport(std::move(R));
}

void ento::registerMallocNullChecker (CheckerManager &mgr) {
  mgr.registerChecker<MallocNullChecker>();
}

// This checker should be enabled regardless of how language options are set.
bool ento::shouldRegisterMallocNullChecker (const CheckerManager &mgr) {
  return true;
}
