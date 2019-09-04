///////////////////////////////////////////////////////////////////////////////
//                                                                           //
// DxilUtil.cpp                                                              //
// Copyright (C) Microsoft Corporation. All rights reserved.                 //
// This file is distributed under the University of Illinois Open Source     //
// License. See LICENSE.TXT for details.                                     //
//                                                                           //
// Dxil helper functions.                                                    //
//                                                                           //
///////////////////////////////////////////////////////////////////////////////


#include "dxc/DXIL/DxilTypeSystem.h"
#include "dxc/DXIL/DxilUtil.h"
#include "dxc/DXIL/DxilModule.h"
#include "dxc/Support/Global.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Bitcode/ReaderWriter.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/DiagnosticPrinter.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DIBuilder.h"
#include "llvm/IR/IRBuilder.h"

using namespace llvm;
using namespace hlsl;

namespace hlsl {

namespace dxilutil {

const char ManglingPrefix[] = "\01?";
const char EntryPrefix[] = "dx.entry.";

Type *GetArrayEltTy(Type *Ty) {
  if (isa<PointerType>(Ty))
    Ty = Ty->getPointerElementType();
  while (isa<ArrayType>(Ty)) {
    Ty = Ty->getArrayElementType();
  }
  return Ty;
}

bool HasDynamicIndexing(Value *V) {
  for (auto User : V->users()) {
    if (GetElementPtrInst *GEP = dyn_cast<GetElementPtrInst>(User)) {
      for (auto Idx = GEP->idx_begin(); Idx != GEP->idx_end(); ++Idx) {
        if (!isa<ConstantInt>(Idx))
          return true;
      }
    }
  }
  return false;
}

unsigned
GetLegacyCBufferFieldElementSize(DxilFieldAnnotation &fieldAnnotation,
                                           llvm::Type *Ty,
                                           DxilTypeSystem &typeSys) {

  while (isa<ArrayType>(Ty)) {
    Ty = Ty->getArrayElementType();
  }

  // Bytes.
  CompType compType = fieldAnnotation.GetCompType();
  unsigned compSize = compType.Is64Bit() ? 8 : compType.Is16Bit() && !typeSys.UseMinPrecision() ? 2 : 4;
  unsigned fieldSize = compSize;
  if (Ty->isVectorTy()) {
    fieldSize *= Ty->getVectorNumElements();
  } else if (StructType *ST = dyn_cast<StructType>(Ty)) {
    DxilStructAnnotation *EltAnnotation = typeSys.GetStructAnnotation(ST);
    if (EltAnnotation) {
      fieldSize = EltAnnotation->GetCBufferSize();
    } else {
      // Calculate size when don't have annotation.
      if (fieldAnnotation.HasMatrixAnnotation()) {
        const DxilMatrixAnnotation &matAnnotation =
            fieldAnnotation.GetMatrixAnnotation();
        unsigned rows = matAnnotation.Rows;
        unsigned cols = matAnnotation.Cols;
        if (matAnnotation.Orientation == MatrixOrientation::ColumnMajor) {
          rows = cols;
          cols = matAnnotation.Rows;
        } else if (matAnnotation.Orientation != MatrixOrientation::RowMajor) {
          // Invalid matrix orientation.
          fieldSize = 0;
        }
        fieldSize = (rows - 1) * 16 + cols * 4;
      } else {
        // Cannot find struct annotation.
        fieldSize = 0;
      }
    }
  }
  return fieldSize;
}

bool IsStaticGlobal(GlobalVariable *GV) {
  return GV->getLinkage() == GlobalValue::LinkageTypes::InternalLinkage &&
         GV->getType()->getPointerAddressSpace() == DXIL::kDefaultAddrSpace;
}

bool IsSharedMemoryGlobal(llvm::GlobalVariable *GV) {
  return GV->getType()->getPointerAddressSpace() == DXIL::kTGSMAddrSpace;
}

bool RemoveUnusedFunctions(Module &M, Function *EntryFunc,
                           Function *PatchConstantFunc, bool IsLib) {
  std::vector<Function *> deadList;
  for (auto &F : M.functions()) {
    if (&F == EntryFunc || &F == PatchConstantFunc)
      continue;
    if (F.isDeclaration() || !IsLib) {
      if (F.user_empty())
        deadList.emplace_back(&F);
    }
  }
  bool bUpdated = deadList.size();
  for (Function *F : deadList)
    F->eraseFromParent();
  return bUpdated;
}

void PrintDiagnosticHandler(const llvm::DiagnosticInfo &DI, void *Context) {
  DiagnosticPrinter *printer = reinterpret_cast<DiagnosticPrinter *>(Context);
  DI.print(*printer);
}

StringRef DemangleFunctionName(StringRef name) {
  if (!name.startswith(ManglingPrefix)) {
    // Name isn't mangled.
    return name;
  }

  size_t nameEnd = name.find_first_of("@");
  DXASSERT(nameEnd != StringRef::npos, "else Name isn't mangled but has \01?");

  return name.substr(2, nameEnd - 2);
}

std::string ReplaceFunctionName(StringRef originalName, StringRef newName) {
  if (originalName.startswith(ManglingPrefix)) {
    return (Twine(ManglingPrefix) + newName +
      originalName.substr(originalName.find_first_of('@'))).str();
  } else if (originalName.startswith(EntryPrefix)) {
    return (Twine(EntryPrefix) + newName).str();
  }
  return newName.str();
}

// From AsmWriter.cpp
// PrintEscapedString - Print each character of the specified string, escaping
// it if it is not printable or if it is an escape char.
void PrintEscapedString(StringRef Name, raw_ostream &Out) {
  for (unsigned i = 0, e = Name.size(); i != e; ++i) {
    unsigned char C = Name[i];
    if (isprint(C) && C != '\\' && C != '"')
      Out << C;
    else
      Out << '\\' << hexdigit(C >> 4) << hexdigit(C & 0x0F);
  }
}

void PrintUnescapedString(StringRef Name, raw_ostream &Out) {
  for (unsigned i = 0, e = Name.size(); i != e; ++i) {
    unsigned char C = Name[i];
    if (C == '\\') {
      C = Name[++i];
      unsigned value = hexDigitValue(C);
      if (value != -1U) {
        C = (unsigned char)value;
        unsigned value2 = hexDigitValue(Name[i+1]);
        assert(value2 != -1U && "otherwise, not a two digit hex escape");
        if (value2 != -1U) {
          C = (C << 4) + (unsigned char)value2;
          ++i;
        }
      } // else, the next character (in C) should be the escaped character
    }
    Out << C;
  }
}

std::unique_ptr<llvm::Module> LoadModuleFromBitcode(llvm::MemoryBuffer *MB,
  llvm::LLVMContext &Ctx,
  std::string &DiagStr) {
  // Note: the DiagStr is not used.
  auto pModule = llvm::parseBitcodeFile(MB->getMemBufferRef(), Ctx);
  if (!pModule) {
    return nullptr;
  }
  return std::unique_ptr<llvm::Module>(pModule.get().release());
}

std::unique_ptr<llvm::Module> LoadModuleFromBitcode(llvm::StringRef BC,
  llvm::LLVMContext &Ctx,
  std::string &DiagStr) {
  std::unique_ptr<llvm::MemoryBuffer> pBitcodeBuf(
    llvm::MemoryBuffer::getMemBuffer(BC, "", false));
  return LoadModuleFromBitcode(pBitcodeBuf.get(), Ctx, DiagStr);
}

// If we don't have debug location and this is select/phi,
// try recursing users to find instruction with debug info.
// Only recurse phi/select and limit depth to prevent doing
// too much work if no debug location found.
static bool EmitErrorOnInstructionFollowPhiSelect(
    Instruction *I, StringRef Msg, unsigned depth=0) {
  if (depth > 4)
    return false;
  if (I->getDebugLoc().get()) {
    EmitErrorOnInstruction(I, Msg);
    return true;
  }
  if (isa<PHINode>(I) || isa<SelectInst>(I)) {
    for (auto U : I->users())
      if (Instruction *UI = dyn_cast<Instruction>(U))
        if (EmitErrorOnInstructionFollowPhiSelect(UI, Msg, depth+1))
          return true;
  }
  return false;
}

std::string FormatMessageAtLocation(const DebugLoc &DL, const Twine& Msg) {
  std::string locString;
  raw_string_ostream os(locString);
  DL.print(os);
  os << ": " << Msg;
  return os.str();
}

Twine FormatMessageWithoutLocation(const Twine& Msg) {
  return Msg + " Use /Zi for source location.";
}

void EmitErrorOnInstruction(Instruction *I, StringRef Msg) {
  const DebugLoc &DL = I->getDebugLoc();
  if (DL.get()) {
    I->getContext().emitError(FormatMessageAtLocation(DL, Msg));
    return;
  } else if (isa<PHINode>(I) || isa<SelectInst>(I)) {
    if (EmitErrorOnInstructionFollowPhiSelect(I, Msg))
      return;
  }

  I->getContext().emitError(FormatMessageWithoutLocation(Msg));
}

const StringRef kResourceMapErrorMsg =
    "local resource not guaranteed to map to unique global resource.";
void EmitResMappingError(Instruction *Res) {
  EmitErrorOnInstruction(Res, kResourceMapErrorMsg);
}

void CollectSelect(llvm::Instruction *Inst,
                   std::unordered_set<llvm::Instruction *> &selectSet) {
  unsigned startOpIdx = 0;
  // Skip Cond for Select.
  if (isa<SelectInst>(Inst)) {
    startOpIdx = 1;
  } else if (!isa<PHINode>(Inst)) {
    // Only check phi and select here.
    return;
  }
  // Already add.
  if (selectSet.count(Inst))
    return;

  selectSet.insert(Inst);

  // Scan operand to add node which is phi/select.
  unsigned numOperands = Inst->getNumOperands();
  for (unsigned i = startOpIdx; i < numOperands; i++) {
    Value *V = Inst->getOperand(i);
    if (Instruction *I = dyn_cast<Instruction>(V)) {
      CollectSelect(I, selectSet);
    }
  }
}

Value *MergeSelectOnSameValue(Instruction *SelInst, unsigned startOpIdx,
                            unsigned numOperands) {
  Value *op0 = nullptr;
  for (unsigned i = startOpIdx; i < numOperands; i++) {
    Value *op = SelInst->getOperand(i);
    if (i == startOpIdx) {
      op0 = op;
    } else {
      if (op0 != op)
        return nullptr;
    }
  }
  if (op0) {
    SelInst->replaceAllUsesWith(op0);
    SelInst->eraseFromParent();
  }
  return op0;
}

bool SimplifyTrivialPHIs(BasicBlock *BB) {
  bool Changed = false;
  SmallVector<Instruction *, 16> Removed;
  for (Instruction &I : *BB) {
    PHINode *PN = dyn_cast<PHINode>(&I);
    if (!PN)
      continue;

    if (PN->getNumIncomingValues() == 1) {
      Value *V = PN->getIncomingValue(0);
      PN->replaceAllUsesWith(V);
      Removed.push_back(PN);
      Changed = true;
    }
  }

  for (Instruction *I : Removed)
    I->eraseFromParent();

  return Changed;
}

static DbgValueInst *FindDbgValueInst(Value *Val) {
  if (auto *ValAsMD = LocalAsMetadata::getIfExists(Val)) {
    if (auto *ValMDAsVal = MetadataAsValue::getIfExists(Val->getContext(), ValAsMD)) {
      for (User *ValMDUser : ValMDAsVal->users()) {
        if (DbgValueInst *DbgValInst = dyn_cast<DbgValueInst>(ValMDUser))
          return DbgValInst;
      }
    }
  }
  return nullptr;
}

void MigrateDebugValue(Value *Old, Value *New) {
  DbgValueInst *DbgValInst = FindDbgValueInst(Old);
  if (DbgValInst == nullptr) return;
  
  DbgValInst->setOperand(0, MetadataAsValue::get(New->getContext(), ValueAsMetadata::get(New)));

  // Move the dbg value after the new instruction
  if (Instruction *NewInst = dyn_cast<Instruction>(New)) {
    if (NewInst->getNextNode() != DbgValInst) {
      DbgValInst->removeFromParent();
      DbgValInst->insertAfter(NewInst);
    }
  }
}

// Propagates any llvm.dbg.value instruction for a given vector
// to the elements that were used to create it through a series
// of insertelement instructions.
//
// This is used after lowering a vector-returning intrinsic.
// If we just keep the debug info on the recomposed vector,
// we will lose it when we break it apart again during later
// optimization stages.
void TryScatterDebugValueToVectorElements(Value *Val) {
  if (!isa<InsertElementInst>(Val) || !Val->getType()->isVectorTy()) return;

  DbgValueInst *VecDbgValInst = FindDbgValueInst(Val);
  if (VecDbgValInst == nullptr) return;

  Type *ElemTy = Val->getType()->getVectorElementType();
  DIBuilder DbgInfoBuilder(*VecDbgValInst->getModule());
  unsigned ElemSizeInBits = VecDbgValInst->getModule()->getDataLayout().getTypeSizeInBits(ElemTy);

  DIExpression *ParentBitPiece = VecDbgValInst->getExpression();
  if (ParentBitPiece != nullptr && !ParentBitPiece->isBitPiece())
    ParentBitPiece = nullptr;

  while (InsertElementInst *InsertElt = dyn_cast<InsertElementInst>(Val)) {
    Value *NewElt = InsertElt->getOperand(1);
    unsigned EltIdx = static_cast<unsigned>(cast<ConstantInt>(InsertElt->getOperand(2))->getLimitedValue());
    unsigned OffsetInBits = EltIdx * ElemSizeInBits;

    if (ParentBitPiece) {
      assert(OffsetInBits + ElemSizeInBits <= ParentBitPiece->getBitPieceSize()
        && "Nested bit piece expression exceeds bounds of its parent.");
      OffsetInBits += ParentBitPiece->getBitPieceOffset();
    }

    DIExpression *DIExpr = DbgInfoBuilder.createBitPieceExpression(OffsetInBits, ElemSizeInBits);
    // Offset is basically unused and deprecated in later LLVM versions.
    // Emit it as zero otherwise later versions of the bitcode reader will drop the intrinsic.
    DbgInfoBuilder.insertDbgValueIntrinsic(NewElt, /* Offset */ 0, VecDbgValInst->getVariable(),
      DIExpr, VecDbgValInst->getDebugLoc(), InsertElt);
    Val = InsertElt->getOperand(0);
  }
}

Value *SelectOnOperation(llvm::Instruction *Inst, unsigned operandIdx) {
  Instruction *prototype = Inst;
  for (unsigned i = 0; i < prototype->getNumOperands(); i++) {
    if (i == operandIdx)
      continue;
    if (!isa<Constant>(prototype->getOperand(i)))
      return nullptr;
  }
  Value *V = prototype->getOperand(operandIdx);
  if (SelectInst *SI = dyn_cast<SelectInst>(V)) {
    IRBuilder<> Builder(SI);
    Instruction *trueClone = Inst->clone();
    trueClone->setOperand(operandIdx, SI->getTrueValue());
    Builder.Insert(trueClone);
    Instruction *falseClone = Inst->clone();
    falseClone->setOperand(operandIdx, SI->getFalseValue());
    Builder.Insert(falseClone);
    Value *newSel =
        Builder.CreateSelect(SI->getCondition(), trueClone, falseClone);
    return newSel;
  }

  if (PHINode *Phi = dyn_cast<PHINode>(V)) {
    Type *Ty = Inst->getType();
    unsigned numOperands = Phi->getNumOperands();
    IRBuilder<> Builder(Phi);
    PHINode *newPhi = Builder.CreatePHI(Ty, numOperands);
    for (unsigned i = 0; i < numOperands; i++) {
      BasicBlock *b = Phi->getIncomingBlock(i);
      Value *V = Phi->getIncomingValue(i);
      Instruction *iClone = Inst->clone();
      IRBuilder<> iBuilder(b->getTerminator()->getPrevNode());
      iClone->setOperand(operandIdx, V);
      iBuilder.Insert(iClone);
      newPhi->addIncoming(iClone, b);
    }
    return newPhi;
  }
  return nullptr;
}

llvm::Instruction *SkipAllocas(llvm::Instruction *I) {
  // Step past any allocas:
  while (I && isa<AllocaInst>(I))
    I = I->getNextNode();
  return I;
}
llvm::Instruction *FindAllocaInsertionPt(llvm::BasicBlock* BB) {
  return &*BB->getFirstInsertionPt();
}
llvm::Instruction *FindAllocaInsertionPt(llvm::Function* F) {
  return FindAllocaInsertionPt(&F->getEntryBlock());
}
llvm::Instruction *FindAllocaInsertionPt(llvm::Instruction* I) {
  Function *F = I->getParent()->getParent();
  if (F)
    return FindAllocaInsertionPt(F);
  else // BB with no parent function
    return FindAllocaInsertionPt(I->getParent());
}
llvm::Instruction *FirstNonAllocaInsertionPt(llvm::Instruction* I) {
  return SkipAllocas(FindAllocaInsertionPt(I));
}
llvm::Instruction *FirstNonAllocaInsertionPt(llvm::BasicBlock* BB) {
  return SkipAllocas(FindAllocaInsertionPt(BB));
}
llvm::Instruction *FirstNonAllocaInsertionPt(llvm::Function* F) {
  return SkipAllocas(FindAllocaInsertionPt(F));
}

bool IsHLSLResourceType(llvm::Type *Ty) {
  if (llvm::StructType *ST = dyn_cast<llvm::StructType>(Ty)) {
    StringRef name = ST->getName();
    name = name.ltrim("class.");
    name = name.ltrim("struct.");

    if (name == "SamplerState")
      return true;
    if (name == "SamplerComparisonState")
      return true;

    if (name.startswith("AppendStructuredBuffer<"))
      return true;
    if (name.startswith("ConsumeStructuredBuffer<"))
      return true;

    if (name.startswith("ConstantBuffer<"))
      return true;

    if (name == "RaytracingAccelerationStructure")
      return true;

    name = name.ltrim("RasterizerOrdered");
    name = name.ltrim("RW");
    if (name == "ByteAddressBuffer")
      return true;

    if (name.startswith("Buffer<"))
      return true;
    if (name.startswith("StructuredBuffer<"))
      return true;

    if (name.startswith("Texture")) {
      name = name.ltrim("Texture");
      if (name.startswith("1D<"))
        return true;
      if (name.startswith("1DArray<"))
        return true;
      if (name.startswith("2D<"))
        return true;
      if (name.startswith("2DArray<"))
        return true;
      if (name.startswith("3D<"))
        return true;
      if (name.startswith("Cube<"))
        return true;
      if (name.startswith("CubeArray<"))
        return true;
      if (name.startswith("2DMS<"))
        return true;
      if (name.startswith("2DMSArray<"))
        return true;
    }
  }
  return false;
}

bool IsHLSLObjectType(llvm::Type *Ty) {
  if (llvm::StructType *ST = dyn_cast<llvm::StructType>(Ty)) {
    StringRef name = ST->getName();
    // TODO: don't check names.
    if (name.startswith("dx.types.wave_t"))
      return true;

    if (name.endswith("_slice_type"))
      return false;

    if (IsHLSLResourceType(Ty))
      return true;

    name = name.ltrim("class.");
    name = name.ltrim("struct.");

    if (name.startswith("TriangleStream<"))
      return true;
    if (name.startswith("PointStream<"))
      return true;
    if (name.startswith("LineStream<"))
      return true;
  }
  return false;
}

bool IsIntegerOrFloatingPointType(llvm::Type *Ty) {
  return Ty->isIntegerTy() || Ty->isFloatingPointTy();
}

bool ContainsHLSLObjectType(llvm::Type *Ty) {
  // Unwrap pointer/array
  while (llvm::isa<llvm::PointerType>(Ty))
    Ty = llvm::cast<llvm::PointerType>(Ty)->getPointerElementType();
  while (llvm::isa<llvm::ArrayType>(Ty))
    Ty = llvm::cast<llvm::ArrayType>(Ty)->getArrayElementType();

  if (llvm::StructType *ST = llvm::dyn_cast<llvm::StructType>(Ty)) {
    if (ST->getName().startswith("dx.types."))
      return true;
    // TODO: How is this suppoed to check for Input/OutputPatch types if
    // these have already been eliminated in function arguments during CG?
    if (IsHLSLObjectType(Ty))
      return true;
    // Otherwise, recurse elements of UDT
    for (auto ETy : ST->elements()) {
      if (ContainsHLSLObjectType(ETy))
        return true;
    }
  }
  return false;
}

// Based on the implementation available in LLVM's trunk:
// http://llvm.org/doxygen/Constants_8cpp_source.html#l02734
bool IsSplat(llvm::ConstantDataVector *cdv) {
  const char *Base = cdv->getRawDataValues().data();

  // Compare elements 1+ to the 0'th element.
  unsigned EltSize = cdv->getElementByteSize();
  for (unsigned i = 1, e = cdv->getNumElements(); i != e; ++i)
    if (memcmp(Base, Base + i * EltSize, EltSize))
      return false;

  return true;
}

}
}

///////////////////////////////////////////////////////////////////////////////

namespace {
class DxilLoadMetadata : public ModulePass {
public:
  static char ID; // Pass identification, replacement for typeid
  explicit DxilLoadMetadata () : ModulePass(ID) {}

  const char *getPassName() const override { return "HLSL load DxilModule from metadata"; }

  bool runOnModule(Module &M) override {
    if (!M.HasDxilModule()) {
      (void)M.GetOrCreateDxilModule();
      return true;
    }

    return false;
  }
};
}

char DxilLoadMetadata::ID = 0;

ModulePass *llvm::createDxilLoadMetadataPass() {
  return new DxilLoadMetadata();
}

INITIALIZE_PASS(DxilLoadMetadata, "hlsl-dxilload", "HLSL load DxilModule from metadata", false, false)
