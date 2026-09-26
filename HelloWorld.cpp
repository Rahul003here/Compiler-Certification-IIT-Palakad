//===-- HelloWorld.cpp - Example Transformations --------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Utils/HelloWorld.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/ConstantFolding.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/Local.h"

#include <map>
#include <string>
#include <utility>

using namespace llvm;

namespace {

//===----------------------------------------------------------------------===//
// 1. Constant propagation
//===----------------------------------------------------------------------===//
bool rahul_constant_propagation(Function &F) {
  bool Changed = false;
  const DataLayout &DL = F.getDataLayout();

  for (BasicBlock &BB : F) {
    for (Instruction &I : make_early_inc_range(BB)) {
      if (I.isTerminator() || I.mayHaveSideEffects())
        continue;

      Constant *Folded = ConstantFoldInstruction(&I, DL);
      if (!Folded || Folded == cast<Value>(&I))
        continue;

      I.replaceAllUsesWith(Folded);
      I.eraseFromParent();
      Changed = true;
    }
  }

  return Changed;
}

//===----------------------------------------------------------------------===//
// Power-of-two helper
//===----------------------------------------------------------------------===//
Instruction *buildPowerOfTwoShift(BinaryOperator *BO) {
  unsigned Opc = BO->getOpcode();
  if (Opc != Instruction::Mul && Opc != Instruction::SDiv &&
      Opc != Instruction::UDiv)
    return nullptr;

  Value *LHS = BO->getOperand(0);
  auto *RHS = dyn_cast<ConstantInt>(BO->getOperand(1));

  if (!RHS && Opc == Instruction::Mul) {
    RHS = dyn_cast<ConstantInt>(LHS);
    if (RHS)
      LHS = BO->getOperand(1);
  }

  if (!RHS)
    return nullptr;

  int Log2 = RHS->getValue().exactLogBase2();
  if (Log2 < 1)
    return nullptr;

  Constant *Amount = ConstantInt::get(LHS->getType(), Log2);

  Instruction::BinaryOps ShiftOp = Instruction::AShr;
  if (Opc == Instruction::Mul)
    ShiftOp = Instruction::Shl;
  else if (Opc == Instruction::UDiv)
    ShiftOp = Instruction::LShr;

  return BinaryOperator::Create(ShiftOp, LHS, Amount, "", BO->getIterator());
}

//===----------------------------------------------------------------------===//
// 2. Instruction combining
//===----------------------------------------------------------------------===//
Value *simplifyBinaryOp(BinaryOperator *BO) {
  Value *LHS = BO->getOperand(0);
  Value *RHS = BO->getOperand(1);
  Type *Ty = BO->getType();

  if (!Ty->isIntegerTy())
    return nullptr;

  if (LHS == RHS) {
    switch (BO->getOpcode()) {
    case Instruction::Sub:
    case Instruction::Xor:
      return Constant::getNullValue(Ty);
    case Instruction::SDiv:
    case Instruction::UDiv:
      return ConstantInt::get(Ty, 1);
    case Instruction::And:
    case Instruction::Or:
      return LHS;
    default:
      break;
    }
  }

  auto *RHSC = dyn_cast<Constant>(RHS);
  auto *LHSC = dyn_cast<Constant>(LHS);

  if (!RHSC && LHSC && BO->isCommutative()) {
    std::swap(LHS, RHS);
    std::swap(LHSC, RHSC);
  }

  if (!RHSC)
    return nullptr;

  switch (BO->getOpcode()) {
  case Instruction::Add:
  case Instruction::Sub:
  case Instruction::Or:
  case Instruction::Xor:
    if (RHSC->isNullValue())
      return LHS;
    break;
  case Instruction::Mul:
    if (RHSC->isNullValue())
      return Constant::getNullValue(Ty);
    if (RHSC->isOneValue())
      return LHS;
    break;
  case Instruction::SDiv:
  case Instruction::UDiv:
    if (RHSC->isOneValue())
      return LHS;
    break;
  case Instruction::And:
    if (RHSC->isNullValue())
      return Constant::getNullValue(Ty);
    break;
  case Instruction::Shl:
  case Instruction::LShr:
  case Instruction::AShr:
    if (RHSC->isNullValue())
      return LHS;
    break;
  default:
    break;
  }

  return nullptr;
}

bool rahul_inst_combine(Function &F) {
  bool Changed = false;

  for (BasicBlock &BB : F) {
    for (Instruction &I : make_early_inc_range(BB)) {
      auto *BO = dyn_cast<BinaryOperator>(&I);
      if (!BO)
        continue;

      if (Value *Simple = simplifyBinaryOp(BO)) {
        BO->replaceAllUsesWith(Simple);
        BO->eraseFromParent();
        Changed = true;
        continue;
      }

      if (Instruction *Shift = buildPowerOfTwoShift(BO)) {
        BO->replaceAllUsesWith(Shift);
        BO->eraseFromParent();
        Changed = true;
      }
    }
  }

  return Changed;
}

//===----------------------------------------------------------------------===//
// 3. Dead code elimination
//===----------------------------------------------------------------------===//
bool rahul_dead_code_elimination(Function &F) {
  bool Changed = false;

  for (BasicBlock &BB : F) {
    SmallVector<Instruction *, 16> Worklist;
    for (Instruction &I : BB)
      Worklist.push_back(&I);

    while (!Worklist.empty()) {
      Instruction *I = Worklist.pop_back_val();
      if (!isInstructionTriviallyDead(I))
        continue;

      for (Value *Op : I->operands())
        if (auto *OpInst = dyn_cast<Instruction>(Op))
          Worklist.push_back(OpInst);

      I->eraseFromParent();
      Changed = true;
    }
  }

  return Changed;
}

//===----------------------------------------------------------------------===//
// 4. Strength reduction
//===----------------------------------------------------------------------===//
bool rahul_strength_reduction(Function &F) {
  bool Changed = false;

  for (BasicBlock &BB : F) {
    for (Instruction &I : make_early_inc_range(BB)) {
      auto *BO = dyn_cast<BinaryOperator>(&I);
      if (!BO)
        continue;

      Instruction *Shift = buildPowerOfTwoShift(BO);
      if (!Shift)
        continue;

      BO->replaceAllUsesWith(Shift);
      BO->eraseFromParent();
      Changed = true;
    }
  }

  return Changed;
}

//===----------------------------------------------------------------------===//
// 5. Common subexpression elimination
//===----------------------------------------------------------------------===//
bool isCSECandidate(Instruction *I) {
  if (I->isTerminator() || I->mayHaveSideEffects() || I->mayReadFromMemory())
    return false;

  return isa<BinaryOperator>(I) || isa<CmpInst>(I) || isa<CastInst>(I) ||
         isa<SelectInst>(I) || isa<GetElementPtrInst>(I);
}

std::string makeExpressionKey(Instruction *I) {
  SmallVector<const void *, 4> Ops;
  for (Value *Op : I->operands())
    Ops.push_back(Op);

  if (I->isCommutative() && Ops.size() == 2 && Ops[1] < Ops[0])
    std::swap(Ops[0], Ops[1]);

  std::string Key;
  raw_string_ostream OS(Key);
  OS << I->getOpcode() << ':' << *I->getType();
  for (const void *Op : Ops)
    OS << ':' << Op;

  if (auto *Cmp = dyn_cast<CmpInst>(I))
    OS << ":pred" << Cmp->getPredicate();

  return Key;
}

bool rahul_cse(Function &F) {
  bool Changed = false;

  for (BasicBlock &BB : F) {
    std::map<std::string, Instruction *> Seen;

    for (Instruction &I : make_early_inc_range(BB)) {
      if (!isCSECandidate(&I))
        continue;

      std::string Key = makeExpressionKey(&I);
      auto It = Seen.find(Key);

      if (It == Seen.end()) {
        Seen[Key] = &I;
        continue;
      }

      I.replaceAllUsesWith(It->second);
      I.eraseFromParent();
      Changed = true;
    }
  }

  return Changed;
}

}

//===----------------------------------------------------------------------===//
// Driver
//===----------------------------------------------------------------------===//
PreservedAnalyses HelloWorldPass::run(Function &F, FunctionAnalysisManager &AM) {
  const unsigned MaxRounds = 8;
  bool ChangedOverall = false;

  for (unsigned Round = 0; Round < MaxRounds; ++Round) {
    bool ChangedThisRound = false;

    ChangedThisRound |= rahul_cse(F);
    ChangedThisRound |= rahul_inst_combine(F);
    ChangedThisRound |= rahul_constant_propagation(F);
    ChangedThisRound |= rahul_strength_reduction(F);
    ChangedThisRound |= rahul_dead_code_elimination(F);

    if (!ChangedThisRound)
      break;

    ChangedOverall = true;
  }

  return ChangedOverall ? PreservedAnalyses::none() : PreservedAnalyses::all();
}
