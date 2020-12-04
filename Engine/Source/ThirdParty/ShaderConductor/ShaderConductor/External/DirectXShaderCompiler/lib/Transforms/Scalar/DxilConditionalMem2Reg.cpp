//===- DxilConditionalMem2Reg.cpp - Mem2Reg that selectively promotes Allocas ----===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/Pass.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Utils/PromoteMemToReg.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/Debug.h"
#include "llvm/IR/LegacyPassManager.h"

#include "dxc/DXIL/DxilUtil.h"
#include "dxc/HLSL/HLModule.h"
#include "llvm/Analysis/DxilValueCache.h"

using namespace llvm;
using namespace hlsl;

static bool ContainsFloatingPointType(Type *Ty) {
  if (Ty->isFloatingPointTy()) {
    return true;
  }
  else if (Ty->isArrayTy()) {
    return ContainsFloatingPointType(Ty->getArrayElementType());
  }
  else if (Ty->isVectorTy()) {
    return ContainsFloatingPointType(Ty->getVectorElementType());
  }
  else if (Ty->isStructTy()) {
    for (unsigned i = 0, NumStructElms = Ty->getStructNumElements(); i < NumStructElms; i++) {
      if (ContainsFloatingPointType(Ty->getStructElementType(i)))
        return true;
    }
  }
  return false;
}

static bool Mem2Reg(Function &F, DominatorTree &DT, AssumptionCache &AC) {
  BasicBlock &BB = F.getEntryBlock();  // Get the entry node for the function
  bool Changed  = false;
  std::vector<AllocaInst*> Allocas;
  while (1) {
    Allocas.clear();

    // Find allocas that are safe to promote, by looking at all instructions in
    // the entry node
    for (BasicBlock::iterator I = BB.begin(), E = --BB.end(); I != E; ++I)
      if (AllocaInst *AI = dyn_cast<AllocaInst>(I))       // Is it an alloca?
        if (isAllocaPromotable(AI) &&
          (!HLModule::HasPreciseAttributeWithMetadata(AI) || !ContainsFloatingPointType(AI->getAllocatedType())))
          Allocas.push_back(AI);

    if (Allocas.empty()) break;

    PromoteMemToReg(Allocas, DT, nullptr, &AC);
    Changed = true;
  }

  return Changed;
}

//
// Special Mem2Reg pass that conditionally promotes or transforms Alloca's.
//
// Anything marked 'dx.precise', will not be promoted because precise markers
// are not propagated to the dxil operations yet and will be lost if alloca
// is removed right now.
//
// Precise Allocas of vectors get scalarized here. It's important we do that
// before Scalarizer pass because promoting the allocas later than that will
// produce vector phi's (disallowed by the validator), which need another
// Scalarizer pass to clean up.
//
class DxilConditionalMem2Reg : public FunctionPass {
public:
  static char ID;

  // Function overrides that resolve options when used for DxOpt
  void applyOptions(PassOptions O) override {
    GetPassOptionBool(O, "NoOpt", &NoOpt, false);
  }
  void dumpConfig(raw_ostream &OS) override {
    FunctionPass::dumpConfig(OS);
    OS << ",NoOpt=" << NoOpt;
  }

  bool NoOpt = false;
  explicit DxilConditionalMem2Reg(bool NoOpt=false) : FunctionPass(ID), NoOpt(NoOpt)
  {
    initializeDxilConditionalMem2RegPass(*PassRegistry::getPassRegistry());
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<DominatorTreeWrapperPass>();
    AU.addRequired<AssumptionCacheTracker>();
    AU.setPreservesCFG();
  }

  // Collect and remove all instructions that use AI, but
  // give up if there are anything other than store, bitcast,
  // memcpy, or GEP.
  static bool TryRemoveUnusedAlloca(AllocaInst *AI) {
    std::vector<Instruction *> WorkList;

    WorkList.push_back(AI);

    for (unsigned i = 0; i < WorkList.size(); i++) {
      Instruction *I = WorkList[i];

      for (User *U : I->users()) {
        Instruction *UI = cast<Instruction>(U);

        unsigned Opcode = UI->getOpcode();
        if (Opcode == Instruction::BitCast ||
          Opcode == Instruction::GetElementPtr ||
          Opcode == Instruction::Store)
        {
          WorkList.push_back(UI);
        }
        else if (MemCpyInst *MC = dyn_cast<MemCpyInst>(UI)) {
          if (MC->getSource() == I) { // MC reads from our alloca
            return false;
          }
          WorkList.push_back(UI);
        }
        else { // Load? PHINode? Assume read.
          return false;
        }
      }
    }

    // Remove all instructions
    for (auto It = WorkList.rbegin(), E = WorkList.rend(); It != E; It++) {
      Instruction *I = *It;
      I->eraseFromParent();
    }

    return true;
  }

  static bool RemoveAllUnusedAllocas(Function &F) {
    std::vector<AllocaInst *> Allocas;
    BasicBlock &EntryBB = *F.begin();
    for (auto It = EntryBB.begin(), E = EntryBB.end(); It != E;) {
      Instruction &I = *(It++);
      if (AllocaInst *AI = dyn_cast<AllocaInst>(&I)) {
        Allocas.push_back(AI);
      }
    }

    bool Changed = false;
    for (AllocaInst *AI : Allocas) {
      Changed |= TryRemoveUnusedAlloca(AI);
    }

    return Changed;
  }

  //
  // Turns all allocas of vector types that are marked with 'dx.precise'
  // and turn them into scalars. For example:
  //
  //    x = alloca <f32 x 4> !dx.precise
  //
  // becomes:
  //
  //    x1 = alloca f32 !dx.precise
  //    x2 = alloca f32 !dx.precise
  //    x3 = alloca f32 !dx.precise
  //    x4 = alloca f32 !dx.precise
  //
  // This function also replaces all stores and loads but leaves everything
  // else alone by generating insertelement and extractelement as appropriate.
  //
  static bool ScalarizePreciseVectorAlloca(Function &F) {
    BasicBlock *Entry = &*F.begin();

    bool Changed = false;
    for (auto it = Entry->begin(); it != Entry->end();) {
      Instruction *I = &*(it++);
      AllocaInst *AI = dyn_cast<AllocaInst>(I);
      if (!AI || !AI->getAllocatedType()->isVectorTy()) continue;
      if (!HLModule::HasPreciseAttributeWithMetadata(AI)) continue;


      IRBuilder<> B(AI);
      VectorType *VTy = cast<VectorType>(AI->getAllocatedType());
      Type *ScalarTy = VTy->getVectorElementType();

      const unsigned VectorSize = VTy->getVectorNumElements();
      SmallVector<AllocaInst *, 32> Elements;

      for (unsigned i = 0; i < VectorSize; i++) {
        AllocaInst *Elem = B.CreateAlloca(ScalarTy);
        hlsl::DxilMDHelper::CopyMetadata(*Elem, *AI);
        Elements.push_back(Elem);
      }

      for (auto it = AI->user_begin(); it != AI->user_end();) {
        User *U = *(it++);
        if (LoadInst *LI = dyn_cast<LoadInst>(U)) {
          B.SetInsertPoint(LI);
          Value *Vec = UndefValue::get(VTy);
          for (unsigned i = 0; i < VectorSize; i++) {
            LoadInst *Elem = B.CreateLoad(Elements[i]);
            hlsl::DxilMDHelper::CopyMetadata(*Elem, *LI);
            Vec = B.CreateInsertElement(Vec, Elem, i);
          }

          LI->replaceAllUsesWith(Vec);
          LI->eraseFromParent();
        }
        else if (StoreInst *Store = dyn_cast<StoreInst>(U)) {
          B.SetInsertPoint(Store);
          Value *Vec = Store->getValueOperand();
          for (unsigned i = 0; i < VectorSize; i++) {
            Value *Elem = B.CreateExtractElement(Vec, i);
            StoreInst *ElemStore = B.CreateStore(Elem, Elements[i]);
            hlsl::DxilMDHelper::CopyMetadata(*ElemStore, *Store);
          }
          Store->eraseFromParent();
        }
        else {
          llvm_unreachable("Cannot handle non-store/load on precise vector allocas");
        }
      }

      AI->eraseFromParent();
      Changed = true;
    }
    return Changed;
  }

  bool runOnFunction(Function &F) override {


    DominatorTree *DT = &getAnalysis<DominatorTreeWrapperPass>().getDomTree();
    AssumptionCache *AC = &getAnalysis<AssumptionCacheTracker>().getAssumptionCache(F);

    bool Changed = false;
    
    Changed |= RemoveAllUnusedAllocas(F);
    Changed |= ScalarizePreciseVectorAlloca(F);
    Changed |= Mem2Reg(F, *DT, *AC);

    return Changed;
  }
};
char DxilConditionalMem2Reg::ID;

Pass *llvm::createDxilConditionalMem2RegPass(bool NoOpt) {
  return new DxilConditionalMem2Reg(NoOpt);
}

INITIALIZE_PASS_BEGIN(DxilConditionalMem2Reg, "dxil-cond-mem2reg", "Dxil Conditional Mem2Reg", false, false)
INITIALIZE_PASS_DEPENDENCY(DominatorTreeWrapperPass)
INITIALIZE_PASS_DEPENDENCY(AssumptionCacheTracker)
INITIALIZE_PASS_END(DxilConditionalMem2Reg, "dxil-cond-mem2reg", "Dxil Conditional Mem2Reg", false, false)
