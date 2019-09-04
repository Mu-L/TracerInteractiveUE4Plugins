///////////////////////////////////////////////////////////////////////////////
//                                                                           //
// DxilPreparePasses.cpp                                                     //
// Copyright (C) Microsoft Corporation. All rights reserved.                 //
// This file is distributed under the University of Illinois Open Source     //
// License. See LICENSE.TXT for details.                                     //
//                                                                           //
// Passes to prepare DxilModule.                                             //
//                                                                           //
///////////////////////////////////////////////////////////////////////////////

#include "dxc/HLSL/DxilGenerationPass.h"
#include "dxc/DXIL/DxilOperations.h"
#include "dxc/HLSL/HLOperations.h"
#include "dxc/DXIL/DxilModule.h"
#include "dxc/Support/Global.h"
#include "dxc/DXIL/DxilTypeSystem.h"
#include "dxc/DXIL/DxilUtil.h"
#include "dxc/DXIL/DxilFunctionProps.h"
#include "llvm/IR/GetElementPtrTypeIterator.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/PassManager.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/Pass.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/Analysis/AssumptionCache.h"
#include <memory>
#include <unordered_set>

using namespace llvm;
using namespace hlsl;

namespace {
class InvalidateUndefResources : public ModulePass {
public:
  static char ID;

  explicit InvalidateUndefResources() : ModulePass(ID) {
    initializeScalarizerPass(*PassRegistry::getPassRegistry());
  }

  const char *getPassName() const override { return "Invalidate undef resources"; }

  bool runOnModule(Module &M) override;
};
}

char InvalidateUndefResources::ID = 0;

ModulePass *llvm::createInvalidateUndefResourcesPass() { return new InvalidateUndefResources(); }

INITIALIZE_PASS(InvalidateUndefResources, "invalidate-undef-resource", "Invalidate undef resources", false, false)

bool InvalidateUndefResources::runOnModule(Module &M) {
  // Undef resources typically indicate uninitialized locals being used
  // in some code path, which we should catch and report. However, some
  // code patterns in large shaders cause dead undef resources to momentarily,
  // which is not an error. We must wait until cleanup passes
  // have run to know whether we must produce an error.
  // However, we can't leave the undef values in because they could eliminated,
  // such as by reading from resources seen in a code path that was not taken.
  // We avoid the problem by replacing undef values by another invalid
  // value that we can identify later.
  for (auto &F : M.functions()) {
    if (GetHLOpcodeGroupByName(&F) == HLOpcodeGroup::HLCreateHandle) {
      Type *ResTy = F.getFunctionType()->getParamType(
        HLOperandIndex::kCreateHandleResourceOpIdx);
      UndefValue *UndefRes = UndefValue::get(ResTy);
      if (!UndefRes->use_empty()) {
        Constant *InvalidRes = ConstantAggregateZero::get(ResTy);
        UndefRes->replaceAllUsesWith(InvalidRes);
      }
    }
  }
  return false;
}

///////////////////////////////////////////////////////////////////////////////

namespace {
class SimplifyInst : public FunctionPass {
public:
  static char ID;

  SimplifyInst() : FunctionPass(ID) {
    initializeScalarizerPass(*PassRegistry::getPassRegistry());
  }

  bool runOnFunction(Function &F) override;

private:
};
}

char SimplifyInst::ID = 0;

FunctionPass *llvm::createSimplifyInstPass() { return new SimplifyInst(); }

INITIALIZE_PASS(SimplifyInst, "simplify-inst", "Simplify Instructions", false, false)

bool SimplifyInst::runOnFunction(Function &F) {
  for (Function::iterator BBI = F.begin(), BBE = F.end(); BBI != BBE; ++BBI) {
    BasicBlock *BB = BBI;
    llvm::SimplifyInstructionsInBlock(BB, nullptr);
  }
  return true;
}

///////////////////////////////////////////////////////////////////////////////

namespace {
class DxilDeadFunctionElimination : public ModulePass {
public:
  static char ID; // Pass identification, replacement for typeid
  explicit DxilDeadFunctionElimination () : ModulePass(ID) {}

  const char *getPassName() const override { return "Remove all unused function except entry from DxilModule"; }

  bool runOnModule(Module &M) override {
    if (M.HasDxilModule()) {
      DxilModule &DM = M.GetDxilModule();

      bool IsLib = DM.GetShaderModel()->IsLib();
      // Remove unused functions except entry and patch constant func.
      // For library profile, only remove unused external functions.
      Function *EntryFunc = DM.GetEntryFunction();
      Function *PatchConstantFunc = DM.GetPatchConstantFunction();

      return dxilutil::RemoveUnusedFunctions(M, EntryFunc, PatchConstantFunc,
                                             IsLib);
    }

    return false;
  }
};
}

char DxilDeadFunctionElimination::ID = 0;

ModulePass *llvm::createDxilDeadFunctionEliminationPass() {
  return new DxilDeadFunctionElimination();
}

INITIALIZE_PASS(DxilDeadFunctionElimination, "dxil-dfe", "Remove all unused function except entry from DxilModule", false, false)

///////////////////////////////////////////////////////////////////////////////

bool CleanupSharedMemoryAddrSpaceCast(Module &M);

namespace {

static void TransferEntryFunctionAttributes(Function *F, Function *NewFunc) {
  // Keep necessary function attributes
  AttributeSet attributeSet = F->getAttributes();
  StringRef attrKind, attrValue;
  if (attributeSet.hasAttribute(AttributeSet::FunctionIndex, DXIL::kFP32DenormKindString)) {
    Attribute attribute = attributeSet.getAttribute(AttributeSet::FunctionIndex, DXIL::kFP32DenormKindString);
    DXASSERT(attribute.isStringAttribute(), "otherwise we have wrong fp-denorm-mode attribute.");
    attrKind = attribute.getKindAsString();
    attrValue = attribute.getValueAsString();
  }
  if (F == NewFunc) {
    NewFunc->removeAttributes(AttributeSet::FunctionIndex, attributeSet);
  }
  if (!attrKind.empty() && !attrValue.empty())
    NewFunc->addFnAttr(attrKind, attrValue);
}

// If this returns non-null, the old function F has been stripped and can be deleted.
static Function *StripFunctionParameter(Function *F, DxilModule &DM,
    DenseMap<const Function *, DISubprogram *> &FunctionDIs) {
  if (F->arg_empty() && F->getReturnType()->isVoidTy()) {
    // This will strip non-entry function attributes
    TransferEntryFunctionAttributes(F, F);
    return nullptr;
  }

  Module &M = *DM.GetModule();
  Type *VoidTy = Type::getVoidTy(M.getContext());
  FunctionType *FT = FunctionType::get(VoidTy, false);
  for (auto &arg : F->args()) {
    if (!arg.user_empty())
      return nullptr;
    DbgDeclareInst *DDI = llvm::FindAllocaDbgDeclare(&arg);
    if (DDI) {
      DDI->eraseFromParent();
    }
  }

  Function *NewFunc = Function::Create(FT, F->getLinkage());
  M.getFunctionList().insert(F, NewFunc);
  // Splice the body of the old function right into the new function.
  NewFunc->getBasicBlockList().splice(NewFunc->begin(), F->getBasicBlockList());

  TransferEntryFunctionAttributes(F, NewFunc);

  // Patch the pointer to LLVM function in debug info descriptor.
  auto DI = FunctionDIs.find(F);
  if (DI != FunctionDIs.end()) {
    DISubprogram *SP = DI->second;
    SP->replaceFunction(NewFunc);
    // Ensure the map is updated so it can be reused on subsequent argument
    // promotions of the same function.
    FunctionDIs.erase(DI);
    FunctionDIs[NewFunc] = SP;
  }
  NewFunc->takeName(F);
  if (DM.HasDxilFunctionProps(F)) {
    DM.ReplaceDxilEntryProps(F, NewFunc);
  }
  DM.GetTypeSystem().EraseFunctionAnnotation(F);
  DM.GetTypeSystem().AddFunctionAnnotation(NewFunc);
  return NewFunc;
}

void CheckInBoundForTGSM(GlobalVariable &GV, const DataLayout &DL) {
  for (User *U : GV.users()) {
    if (GetElementPtrInst *GEP = dyn_cast<GetElementPtrInst>(U)) {
      bool allImmIndex = true;
      for (auto Idx = GEP->idx_begin(), E = GEP->idx_end(); Idx != E; Idx++) {
        if (!isa<ConstantInt>(Idx)) {
          allImmIndex = false;
          break;
        }
      }
      if (!allImmIndex)
        GEP->setIsInBounds(false);
      else {
        Value *Ptr = GEP->getPointerOperand();
        unsigned size =
            DL.getTypeAllocSize(Ptr->getType()->getPointerElementType());
        unsigned valSize =
            DL.getTypeAllocSize(GEP->getType()->getPointerElementType());
        SmallVector<Value *, 8> Indices(GEP->idx_begin(), GEP->idx_end());
        unsigned offset =
            DL.getIndexedOffset(GEP->getPointerOperandType(), Indices);
        if ((offset + valSize) > size)
          GEP->setIsInBounds(false);
      }
    }
  }
}

class DxilFinalizeModule : public ModulePass {
public:
  static char ID; // Pass identification, replacement for typeid
  explicit DxilFinalizeModule() : ModulePass(ID) {}

  const char *getPassName() const override { return "HLSL DXIL Finalize Module"; }

  void patchValidation_1_1(Module &M) {
    for (iplist<Function>::iterator F : M.getFunctionList()) {
      for (Function::iterator BBI = F->begin(), BBE = F->end(); BBI != BBE;
           ++BBI) {
        BasicBlock *BB = BBI;
        for (BasicBlock::iterator II = BB->begin(), IE = BB->end(); II != IE;
             ++II) {
          Instruction *I = II;
          if (I->hasMetadataOtherThanDebugLoc()) {
            SmallVector<std::pair<unsigned, MDNode*>, 2> MDs;
            I->getAllMetadataOtherThanDebugLoc(MDs);
            for (auto &MD : MDs) {
              unsigned kind = MD.first;
              // Remove Metadata which validation_1_0 not allowed.
              bool bNeedPatch = kind == LLVMContext::MD_tbaa ||
                  kind == LLVMContext::MD_prof ||
                  (kind > LLVMContext::MD_fpmath &&
                  kind <= LLVMContext::MD_dereferenceable_or_null);
              if (bNeedPatch)
                I->setMetadata(kind, nullptr);
            }
          }
        }
      }
    }
  }

  bool runOnModule(Module &M) override {
    if (M.HasDxilModule()) {
      DxilModule &DM = M.GetDxilModule();

      bool IsLib = DM.GetShaderModel()->IsLib();
      // Skip validation patch for lib.
      if (!IsLib) {
        unsigned ValMajor = 0;
        unsigned ValMinor = 0;
        M.GetDxilModule().GetValidatorVersion(ValMajor, ValMinor);
        if (ValMajor == 1 && ValMinor <= 1) {
          patchValidation_1_1(M);
        }
      }

      // Remove store undef output.
      hlsl::OP *hlslOP = M.GetDxilModule().GetOP();
      RemoveStoreUndefOutput(M, hlslOP);

      RemoveUnusedStaticGlobal(M);

      // Remove unnecessary address space casts.
      CleanupSharedMemoryAddrSpaceCast(M);

      // Clear inbound for GEP which has none-const index.
      LegalizeSharedMemoryGEPInbound(M);

      // Strip parameters of entry function.
      StripEntryParameters(M, DM, IsLib);

      // Update flags to reflect any changes.
      DM.CollectShaderFlagsForModule();

      // Update Validator Version
      DM.UpgradeToMinValidatorVersion();

      // Clear intermediate options that shouldn't be in the final DXIL
      DM.ClearIntermediateOptions();

      return true;
    }

    return false;
  }

private:
  void RemoveUnusedStaticGlobal(Module &M) {
    // Remove unused internal global.
    std::vector<GlobalVariable *> staticGVs;
    for (GlobalVariable &GV : M.globals()) {
      if (dxilutil::IsStaticGlobal(&GV) ||
          dxilutil::IsSharedMemoryGlobal(&GV)) {
        staticGVs.emplace_back(&GV);
      }
    }

    for (GlobalVariable *GV : staticGVs) {
      bool onlyStoreUse = true;
      for (User *user : GV->users()) {
        if (isa<StoreInst>(user))
          continue;
        if (isa<ConstantExpr>(user) && user->user_empty())
          continue;
        onlyStoreUse = false;
        break;
      }
      if (onlyStoreUse) {
        for (auto UserIt = GV->user_begin(); UserIt != GV->user_end();) {
          Value *User = *(UserIt++);
          if (Instruction *I = dyn_cast<Instruction>(User)) {
            I->eraseFromParent();
          } else {
            ConstantExpr *CE = cast<ConstantExpr>(User);
            CE->dropAllReferences();
          }
        }
        GV->eraseFromParent();
      }
    }
  }

  void RemoveStoreUndefOutput(Module &M, hlsl::OP *hlslOP) {
    for (iplist<Function>::iterator F : M.getFunctionList()) {
      if (!hlslOP->IsDxilOpFunc(F))
        continue;
      DXIL::OpCodeClass opClass;
      bool bHasOpClass = hlslOP->GetOpCodeClass(F, opClass);
      DXASSERT_LOCALVAR(bHasOpClass, bHasOpClass, "else not a dxil op func");
      if (opClass != DXIL::OpCodeClass::StoreOutput)
        continue;

      for (auto it = F->user_begin(); it != F->user_end();) {
        CallInst *CI = dyn_cast<CallInst>(*(it++));
        if (!CI)
          continue;

        Value *V = CI->getArgOperand(DXIL::OperandIndex::kStoreOutputValOpIdx);
        // Remove the store of undef.
        if (isa<UndefValue>(V))
          CI->eraseFromParent();
      }
    }
  }

  void LegalizeSharedMemoryGEPInbound(Module &M) {
    const DataLayout &DL = M.getDataLayout();
    // Clear inbound for GEP which has none-const index.
    for (GlobalVariable &GV : M.globals()) {
      if (dxilutil::IsSharedMemoryGlobal(&GV)) {
        CheckInBoundForTGSM(GV, DL);
      }
    }
  }

  void StripEntryParameters(Module &M, DxilModule &DM, bool IsLib) {
    DenseMap<const Function *, DISubprogram *> FunctionDIs =
        makeSubprogramMap(M);
    // Strip parameters of entry function.
    if (!IsLib) {
      if (Function *OldPatchConstantFunc = DM.GetPatchConstantFunction()) {
        Function *NewPatchConstantFunc =
            StripFunctionParameter(OldPatchConstantFunc, DM, FunctionDIs);
        if (NewPatchConstantFunc) {
          DM.SetPatchConstantFunction(NewPatchConstantFunc);

          // Erase once the DxilModule doesn't track the old function anymore
          DXASSERT(DM.IsPatchConstantShader(NewPatchConstantFunc) && !DM.IsPatchConstantShader(OldPatchConstantFunc),
            "Error while migrating to parameter-stripped patch constant function.");
          OldPatchConstantFunc->eraseFromParent();
        }
      }

      if (Function *OldEntryFunc = DM.GetEntryFunction()) {
        StringRef Name = DM.GetEntryFunctionName();
        OldEntryFunc->setName(Name);
        Function *NewEntryFunc = StripFunctionParameter(OldEntryFunc, DM, FunctionDIs);
        if (NewEntryFunc) {
          DM.SetEntryFunction(NewEntryFunc);
          OldEntryFunc->eraseFromParent();
        }
      }
    } else {
      std::vector<Function *> entries;
      // Handle when multiple hull shaders point to the same patch constant function
      MapVector<Function*, llvm::SmallVector<Function*, 2>> PatchConstantFuncUsers;
      for (iplist<Function>::iterator F : M.getFunctionList()) {
        if (DM.IsEntryThatUsesSignatures(F)) {
          auto *FT = F->getFunctionType();
          // Only do this when has parameters.
          if (FT->getNumParams() > 0 || !FT->getReturnType()->isVoidTy()) {
            entries.emplace_back(F);
          }

          DxilFunctionProps& props = DM.GetDxilFunctionProps(F);
          if (props.IsHS() && props.ShaderProps.HS.patchConstantFunc) {
            FunctionType* PatchConstantFuncTy = props.ShaderProps.HS.patchConstantFunc->getFunctionType();
            if (PatchConstantFuncTy->getNumParams() > 0 || !PatchConstantFuncTy->getReturnType()->isVoidTy()) {
              // Accumulate all hull shaders using a given patch constant function,
              // so we can update it once and fix all hull shaders, without having an intermediary
              // state where some hull shaders point to a destroyed patch constant function.
              PatchConstantFuncUsers[props.ShaderProps.HS.patchConstantFunc].emplace_back(F);
            }
          }
        }
      }

      // Strip patch constant functions first
      for (auto &PatchConstantFuncEntry : PatchConstantFuncUsers) {
        Function* OldPatchConstantFunc = PatchConstantFuncEntry.first;
        Function* NewPatchConstantFunc = StripFunctionParameter(OldPatchConstantFunc, DM, FunctionDIs);
        if (NewPatchConstantFunc) {
          // Update all user hull shaders
          for (Function *HullShaderFunc : PatchConstantFuncEntry.second)
            DM.SetPatchConstantFunctionForHS(HullShaderFunc, NewPatchConstantFunc);

          // Erase once the DxilModule doesn't track the old function anymore
          DXASSERT(DM.IsPatchConstantShader(NewPatchConstantFunc) && !DM.IsPatchConstantShader(OldPatchConstantFunc),
            "Error while migrating to parameter-stripped patch constant function.");
          OldPatchConstantFunc->eraseFromParent();
        }
      }

      for (Function *OldEntry : entries) {
        Function *NewEntry = StripFunctionParameter(OldEntry, DM, FunctionDIs);
        if (NewEntry) OldEntry->eraseFromParent();
      }
    }
  }
};
}

char DxilFinalizeModule::ID = 0;

ModulePass *llvm::createDxilFinalizeModulePass() {
  return new DxilFinalizeModule();
}

INITIALIZE_PASS(DxilFinalizeModule, "hlsl-dxilfinalize", "HLSL DXIL Finalize Module", false, false)


///////////////////////////////////////////////////////////////////////////////

namespace {
typedef MapVector< PHINode*, SmallVector<Value*,8> > PHIReplacementMap;
bool RemoveAddrSpaceCasts(Value *Val, Value *NewVal,
                          PHIReplacementMap &phiReplacements,
                          DenseMap<Value*, Value*> &valueMap) {
  bool bChanged = false;
  for (auto itU = Val->use_begin(), itEnd = Val->use_end(); itU != itEnd; ) {
    Use &use = *(itU++);
    User *user = use.getUser();
    Value *userReplacement = user;
    bool bConstructReplacement = false;
    bool bCleanupInst = false;
    auto valueMapIter = valueMap.find(user);
    if (valueMapIter != valueMap.end())
      userReplacement = valueMapIter->second;
    else if (Val != NewVal)
      bConstructReplacement = true;
    if (ConstantExpr* CE = dyn_cast<ConstantExpr>(user)) {
      if (CE->getOpcode() == Instruction::BitCast) {
        if (bConstructReplacement) {
          // Replicate bitcast in target address space
          Type* NewTy = PointerType::get(
            CE->getType()->getPointerElementType(),
            NewVal->getType()->getPointerAddressSpace());
          userReplacement = ConstantExpr::getBitCast(cast<Constant>(NewVal), NewTy);
        }
      } else if (CE->getOpcode() == Instruction::GetElementPtr) {
        if (bConstructReplacement) {
          // Replicate GEP in target address space
          GEPOperator *GEP = cast<GEPOperator>(CE);
          SmallVector<Value*, 8> idxList(GEP->idx_begin(), GEP->idx_end());
          userReplacement = ConstantExpr::getGetElementPtr(
            nullptr, cast<Constant>(NewVal), idxList, GEP->isInBounds());
        }
      } else if (CE->getOpcode() == Instruction::AddrSpaceCast) {
        userReplacement = NewVal;
        bConstructReplacement = false;
      } else {
        DXASSERT(false, "RemoveAddrSpaceCasts: unhandled pointer ConstantExpr");
      }
    } else if (Instruction *I = dyn_cast<Instruction>(user)) {
      if (GetElementPtrInst *GEP = dyn_cast<GetElementPtrInst>(user)) {
        if (bConstructReplacement) {
          IRBuilder<> Builder(GEP);
          SmallVector<Value*, 8> idxList(GEP->idx_begin(), GEP->idx_end());
          if (GEP->isInBounds())
            userReplacement = Builder.CreateInBoundsGEP(NewVal, idxList, GEP->getName());
          else
            userReplacement = Builder.CreateGEP(NewVal, idxList, GEP->getName());
        }
      } else if (BitCastInst *BC = dyn_cast<BitCastInst>(user)) {
        if (bConstructReplacement) {
          IRBuilder<> Builder(BC);
          Type* NewTy = PointerType::get(
            BC->getType()->getPointerElementType(),
            NewVal->getType()->getPointerAddressSpace());
          userReplacement = Builder.CreateBitCast(NewVal, NewTy);
        }
      } else if (PHINode *PHI = dyn_cast<PHINode>(user)) {
        // set replacement phi values for PHI pass
        unsigned numValues = PHI->getNumIncomingValues();
        auto &phiValues = phiReplacements[PHI];
        if (phiValues.empty())
          phiValues.resize(numValues, nullptr);
        for (unsigned idx = 0; idx < numValues; ++idx) {
          if (phiValues[idx] == nullptr &&
              PHI->getIncomingValue(idx) == Val) {
            phiValues[idx] = NewVal;
            bChanged = true;
          }
        }
        continue;
      } else if (isa<AddrSpaceCastInst>(user)) {
        userReplacement = NewVal;
        bConstructReplacement = false;
        bCleanupInst = true;
      } else if (isa<CallInst>(user)) {
        continue;
      } else {
        if (Val != NewVal) {
          use.set(NewVal);
          bChanged = true;
        }
        continue;
      }
    }
    if (bConstructReplacement && user != userReplacement)
      valueMap[user] = userReplacement;
    bChanged |= RemoveAddrSpaceCasts(user, userReplacement, phiReplacements,
                                      valueMap);
    if (bCleanupInst && user->use_empty()) {
      // Clean up old instruction if it's now unused.
      // Safe during this use iteration when only one use of V in instruction.
      if (Instruction *I = dyn_cast<Instruction>(user))
        I->eraseFromParent();
      bChanged = true;
    }
  }
  return bChanged;
}
}

bool CleanupSharedMemoryAddrSpaceCast(Module &M) {
  bool bChanged = false;
  // Eliminate address space casts if possible
  // Collect phi nodes so we can replace iteratively after pass over GVs
  PHIReplacementMap phiReplacements;
  DenseMap<Value*, Value*> valueMap;
  for (GlobalVariable &GV : M.globals()) {
    if (dxilutil::IsSharedMemoryGlobal(&GV)) {
      bChanged |= RemoveAddrSpaceCasts(&GV, &GV, phiReplacements,
                                       valueMap);
    }
  }
  bool bConverged = false;
  while (!phiReplacements.empty() && !bConverged) {
    bConverged = true;
    for (auto &phiReplacement : phiReplacements) {
      PHINode *PHI = phiReplacement.first;
      unsigned origAddrSpace = PHI->getType()->getPointerAddressSpace();
      unsigned incomingAddrSpace = UINT_MAX;
      bool bReplacePHI = true;
      bool bRemovePHI = false;
      for (auto V : phiReplacement.second) {
        if (nullptr == V) {
          // cannot replace phi (yet)
          bReplacePHI = false;
          break;
        }
        unsigned addrSpace = V->getType()->getPointerAddressSpace();
        if (incomingAddrSpace == UINT_MAX) {
          incomingAddrSpace = addrSpace;
        } else if (addrSpace != incomingAddrSpace) {
          bRemovePHI = true;
          break;
        }
      }
      if (origAddrSpace == incomingAddrSpace)
        bRemovePHI = true;
      if (bRemovePHI) {
        // Cannot replace phi.  Remove it and restart.
        phiReplacements.erase(PHI);
        bConverged = false;
        break;
      }
      if (!bReplacePHI)
        continue;
      auto &NewVal = valueMap[PHI];
      PHINode *NewPHI = nullptr;
      if (NewVal) {
        NewPHI = cast<PHINode>(NewVal);
      } else {
        IRBuilder<> Builder(PHI);
        NewPHI = Builder.CreatePHI(
          PointerType::get(PHI->getType()->getPointerElementType(),
                           incomingAddrSpace),
          PHI->getNumIncomingValues(),
          PHI->getName());
        NewVal = NewPHI;
        for (unsigned idx = 0; idx < PHI->getNumIncomingValues(); idx++) {
          NewPHI->addIncoming(phiReplacement.second[idx],
                              PHI->getIncomingBlock(idx));
        }
      }
      if (RemoveAddrSpaceCasts(PHI, NewPHI, phiReplacements,
                               valueMap)) {
        bConverged = false;
        bChanged = true;
        break;
      }
      if (PHI->use_empty()) {
        phiReplacements.erase(PHI);
        bConverged = false;
        bChanged = true;
        break;
      }
    }
  }

  // Cleanup unused replacement instructions
  SmallVector<WeakVH, 8> cleanupInsts;
  for (auto it : valueMap) {
    if (isa<Instruction>(it.first))
      cleanupInsts.push_back(it.first);
    if (isa<Instruction>(it.second))
      cleanupInsts.push_back(it.second);
  }
  for (auto V : cleanupInsts) {
    if (!V)
      continue;
    if (PHINode *PHI = dyn_cast<PHINode>(V))
      RecursivelyDeleteDeadPHINode(PHI);
    else if (Instruction *I = dyn_cast<Instruction>(V))
      RecursivelyDeleteTriviallyDeadInstructions(I);
  }

  return bChanged;
}

class DxilCleanupAddrSpaceCast : public ModulePass {
public:
  static char ID; // Pass identification, replacement for typeid
  explicit DxilCleanupAddrSpaceCast() : ModulePass(ID) {}

  const char *getPassName() const override { return "HLSL DXIL Cleanup Address Space Cast"; }

  bool runOnModule(Module &M) override {
    return CleanupSharedMemoryAddrSpaceCast(M);
  }
};

char DxilCleanupAddrSpaceCast::ID = 0;

ModulePass *llvm::createDxilCleanupAddrSpaceCastPass() {
  return new DxilCleanupAddrSpaceCast();
}

INITIALIZE_PASS(DxilCleanupAddrSpaceCast, "hlsl-dxil-cleanup-addrspacecast", "HLSL DXIL Cleanup Address Space Cast", false, false)

///////////////////////////////////////////////////////////////////////////////

namespace {

class DxilEmitMetadata : public ModulePass {
public:
  static char ID; // Pass identification, replacement for typeid
  explicit DxilEmitMetadata() : ModulePass(ID) {}

  const char *getPassName() const override { return "HLSL DXIL Metadata Emit"; }

  bool runOnModule(Module &M) override {
    if (M.HasDxilModule()) {
      DxilModule::ClearDxilMetadata(M);
      patchIsFrontfaceTy(M);
      M.GetDxilModule().EmitDxilMetadata();
      return true;
    }

    return false;
  }
private:
  void patchIsFrontfaceTy(Module &M);
};

void patchIsFrontface(DxilSignatureElement &Elt, bool bForceUint) {
  // If force to uint, change i1 to u32.
  // If not force to uint, change u32 to i1.
  if (bForceUint && Elt.GetCompType() == CompType::Kind::I1)
    Elt.SetCompType(CompType::Kind::U32);
  else if (!bForceUint && Elt.GetCompType() == CompType::Kind::U32)
    Elt.SetCompType(CompType::Kind::I1);
}

void patchIsFrontface(DxilSignature &sig, bool bForceUint) {
  for (auto &Elt : sig.GetElements()) {
    if (Elt->GetSemantic()->GetKind() == Semantic::Kind::IsFrontFace) {
      patchIsFrontface(*Elt, bForceUint);
    }
  }
}

void DxilEmitMetadata::patchIsFrontfaceTy(Module &M) {
  DxilModule &DM = M.GetDxilModule();
  const ShaderModel *pSM = DM.GetShaderModel();
  if (!pSM->IsGS() && !pSM->IsPS())
    return;
  unsigned ValMajor, ValMinor;
  DM.GetValidatorVersion(ValMajor, ValMinor);
  bool bForceUint = ValMajor == 0 || (ValMajor >= 1 && ValMinor >= 2);
  if (pSM->IsPS()) {
    patchIsFrontface(DM.GetInputSignature(), bForceUint);
  } else if (pSM->IsGS()) {
    patchIsFrontface(DM.GetOutputSignature(), bForceUint);
  }
}

}

char DxilEmitMetadata::ID = 0;

ModulePass *llvm::createDxilEmitMetadataPass() {
  return new DxilEmitMetadata();
}

INITIALIZE_PASS(DxilEmitMetadata, "hlsl-dxilemit", "HLSL DXIL Metadata Emit", false, false)
