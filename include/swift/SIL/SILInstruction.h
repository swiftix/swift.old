//===--- SILInstruction.h - Instructions for SIL code -----------*- C++ -*-===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2014 - 2015 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//
//
// This file defines the high-level SILInstruction class used for SIL code.
//
//===----------------------------------------------------------------------===//

#ifndef SWIFT_SIL_INSTRUCTION_H
#define SWIFT_SIL_INSTRUCTION_H

#include "llvm/ADT/ilist_node.h"
#include "llvm/ADT/ilist.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/APInt.h"
#include "swift/AST/Builtins.h"
#include "swift/SIL/Consumption.h"
#include "swift/SIL/SILAllocated.h"
#include "swift/SIL/SILLocation.h"
#include "swift/SIL/SILSuccessor.h"
#include "swift/SIL/SILDeclRef.h"
#include "swift/SIL/SILValue.h"

namespace swift {

class DeclRefExpr;
class FloatLiteralExpr;
class FuncDecl;
class IntegerLiteralExpr;
class SILBasicBlock;
class SILBuilder;
class SILDebugLocation;
class SILDebugScope;
class SILFunction;
class SILGlobalVariable;
class SILType;
class SILArgument;
class Stmt;
class StringLiteralExpr;
class Substitution;
class ValueDecl;
class VarDecl;
class FunctionRefInst;

template <typename ImplClass> class SILClonerWithScopes;

/// This is the root class for all instructions that can be used as the contents
/// of a Swift SILBasicBlock.
class SILInstruction : public ValueBase,public llvm::ilist_node<SILInstruction>{
  friend struct llvm::ilist_traits<SILInstruction>;
  friend struct llvm::ilist_traits<SILBasicBlock>;

  /// A backreference to the containing basic block.  This is maintained by
  /// ilist_traits<SILInstruction>.
  SILBasicBlock *ParentBB;

  /// This instruction's containing lexical scope and source location
  /// used for debug info and diagnostics.
  SILDebugLocation &Location;

  friend struct llvm::ilist_sentinel_traits<SILInstruction>;
  SILInstruction() = delete;
  void operator=(const SILInstruction &) = delete;
  void operator delete(void *Ptr, size_t) = delete;

  /// Check any special state of instructions that are not represented in the
  /// instructions operands/type.
  bool hasIdenticalState(const SILInstruction *RHS) const;

  /// Update this instruction's SILDebugScope. This function should
  /// never be called directly. Use SILBuilder, SILBuilderWithScope or
  /// SILClonerWithScope instead.
  void setDebugScope(SILBuilder &B, const SILDebugScope *DS);

protected:
  SILInstruction(ValueKind Kind, SILDebugLocation *DebugLoc, SILType Ty)
      : ValueBase(Kind, Ty), ParentBB(0), Location(*DebugLoc) {}
  SILInstruction(ValueKind Kind, SILDebugLocation *DebugLoc,
                 SILTypeList *TypeList = nullptr)
      : ValueBase(Kind, TypeList), ParentBB(0), Location(*DebugLoc) {}

public:

  enum class MemoryBehavior {
    None,
    /// The instruction may read memory.
    MayRead,
    /// \brief The instruction may write to memory.
    MayWrite,
    /// The instruction may read or write memory.
    MayReadWrite,
    /// \brief The instruction may have side effects not captured
    ///        solely by its users. Specifically, it can return,
    ///        release memory, or store. Note, alloc is not considered
    ///        to have side effects because its result/users represent
    ///        its effect.
    MayHaveSideEffects,
  };

  /// Enumeration representing whether the execution of an instruction can
  /// result in memory being released.
  enum class ReleasingBehavior {
    DoesNotRelease,
    MayRelease,
  };

  const SILBasicBlock *getParent() const { return ParentBB; }
  SILBasicBlock *getParent() { return ParentBB; }

  SILFunction *getFunction();
  const SILFunction *getFunction() const;

  SILModule &getModule() const;

  /// This instruction's source location (AST node).
  SILLocation getLoc() const;
  const SILDebugScope *getDebugScope() const;
  SILDebugLocation &getDebugLocation() const { return Location; }

  /// removeFromParent - This method unlinks 'self' from the containing basic
  /// block, but does not delete it.
  ///
  void removeFromParent();

  /// eraseFromParent - This method unlinks 'self' from the containing basic
  /// block and deletes it.
  ///
  void eraseFromParent();

  /// Unlink this instruction from its current basic block and insert it into
  /// the basic block that Later lives in, right before Later.
  void moveBefore(SILInstruction *Later);

  /// Unlink this instruction from its current basic block and insert it into
  /// the basic block that Earlier lives in, right after Earlier.
  void moveAfter(SILInstruction *Earlier);

  /// \brief Drops all uses that belong to this instruction.
  void dropAllReferences();

  /// \brief Replace all uses of this instruction with Undef.
  ///
  /// TODO: This should be on ValueBase, but ValueBase currently does not have
  /// access to a SILModule. If that ever changes, this method should move to
  /// ValueBase.
  void replaceAllUsesWithUndef();

  /// Return the array of operands for this instruction.
  ArrayRef<Operand> getAllOperands() const;

  /// Return the array of mutable operands for this instruction.
  MutableArrayRef<Operand> getAllOperands();

  unsigned getNumOperands() const { return getAllOperands().size(); }
  SILValue getOperand(unsigned Num) const { return getAllOperands()[Num].get();}
  void setOperand(unsigned Num, SILValue V) { getAllOperands()[Num].set(V); }
  void swapOperands(unsigned Num1, unsigned Num2) {
    getAllOperands()[Num1].swap(getAllOperands()[Num2]);
  }

  MemoryBehavior getMemoryBehavior() const;
  ReleasingBehavior getReleasingBehavior() const;
  bool mayRelease() const;

  /// Can this instruction abort the program in some manner?
  bool mayTrap() const;

  /// Returns true if the given instruction is completely identical to RHS.
  bool isIdenticalTo(const SILInstruction *RHS) const {
    return isIdenticalTo(RHS,
                         [](const SILValue &Op1, const SILValue &Op2) -> bool {
                           return Op1 == Op2; });
  }
  
  /// Returns true if the given instruction is completely identical to RHS,
  /// using \p opEqual to compare operands.
  ///
  template <typename OpCmp>
  bool isIdenticalTo(const SILInstruction *RHS, OpCmp opEqual) const {
    // Quick check if both instructions have the same kind, number of operands,
    // and number of types. This should filter out most cases.
    if (getKind() != RHS->getKind() ||
        getNumOperands() != RHS->getNumOperands() ||
        getNumTypes() != RHS->getNumTypes()) {
      return false;
    }
    
    // Check types.
    //
    // Many instructions have only 1 type so it makes sense to check it first.
    for (unsigned i = 0, e = getNumTypes(); i != e; ++i)
      if (getType(i) != RHS->getType(i))
        return false;
    
    // Check operands.
    for (unsigned i = 0, e = getNumOperands(); i != e; ++i)
      if (!opEqual(getOperand(i), RHS->getOperand(i)))
        return false;

    // Check any special state of instructions that are not represented in the
    // instructions operands/type.
    return hasIdenticalState(RHS);
  }

  /// \brief Returns true if the instruction may have side effects.
  ///
  /// Instructions that store into memory or change retain counts as well as
  /// calls and deallocation instructions are considered to have side effects
  /// that are not visible by merely examining their uses.
  bool mayHaveSideEffects() const;

  /// Returns true if the instruction may write to memory.
  bool mayWriteToMemory() const {
    MemoryBehavior B = getMemoryBehavior();
    return B == MemoryBehavior::MayWrite ||
      B == MemoryBehavior::MayReadWrite ||
      B == MemoryBehavior::MayHaveSideEffects;
  }

  /// Returns true if the instruction may read from memory.
  bool mayReadFromMemory() const {
    MemoryBehavior B = getMemoryBehavior();
    return B == MemoryBehavior::MayRead ||
      B == MemoryBehavior::MayReadWrite ||
      B == MemoryBehavior::MayHaveSideEffects;
  }

  /// Returns true if the instruction may read from or write to memory.
  bool mayReadOrWriteMemory() const {
    return getMemoryBehavior() != MemoryBehavior::None;
  }

  /// Returns true if the result of this instruction is a pointer to stack
  /// allocated memory. In this case there must be an adjacent deallocating
  /// instruction.
  bool isAllocatingStack() const;

  /// Returns true if this is the deallocation of a stack allocating instruction.
  /// The first operand must be the allocating instruction.
  bool isDeallocatingStack() const;

  static bool classof(const ValueBase *V) {
    return V->getKind() >= ValueKind::First_SILInstruction &&
           V->getKind() <= ValueKind::Last_SILInstruction;
  }

  /// Create a new copy of this instruction, which retains all of the operands
  /// and other information of this one.  If an insertion point is specified,
  /// then the new instruction is inserted before the specified point, otherwise
  /// the new instruction is returned without a parent.
  SILInstruction *clone(SILInstruction *InsertPt = nullptr);

  /// Invoke an Instruction's destructor. This dispatches to the appropriate
  /// leaf class destructor for the type of the instruction. This does not
  /// deallocate the instruction.
  static void destroy(SILInstruction *I);

  /// Returns true if the instruction can be duplicated without any special
  /// additional handling. It is important to know this information when
  /// you perform such optimizations like e.g. jump-threading.
  bool isTriviallyDuplicatable() const;
};

#ifndef NDEBUG
/// Pretty-print the MemoryBehavior.
llvm::raw_ostream &operator<<(llvm::raw_ostream &OS,
                              SILInstruction::MemoryBehavior B);
#endif

/// A template base class for instructions that take a single SILValue operand
/// and has no result or a single value result.
template<ValueKind KIND, typename BASE = SILInstruction, bool HAS_RESULT = true>
class UnaryInstructionBase : public BASE {
  FixedOperandList<1> Operands;

  /// Check HAS_RESULT in enable_if predicates by injecting a dependency on
  /// a template argument.
  template<typename X>
  struct has_result {
    enum { value = HAS_RESULT };
  };

public:
  UnaryInstructionBase(SILDebugLocation *DebugLoc, SILValue Operand)
      : BASE(KIND, DebugLoc), Operands(this, Operand) {}

  template <typename X = void>
  UnaryInstructionBase(
      SILDebugLocation *DebugLoc, SILValue Operand,
      typename std::enable_if<has_result<X>::value, SILType>::type Ty)
      : BASE(KIND, DebugLoc, Ty), Operands(this, Operand) {}

  template <typename X = void, typename... A>
  UnaryInstructionBase(
      SILDebugLocation *DebugLoc, SILValue Operand,
      typename std::enable_if<has_result<X>::value, SILType>::type Ty,
      A &&... args)
      : BASE(KIND, DebugLoc, Ty, std::forward<A>(args)...),
        Operands(this, Operand) {}

  SILValue getOperand() const { return Operands[0].get(); }
  void setOperand(SILValue V) { Operands[0].set(V); }

  Operand &getOperandRef() { return Operands[0]; }

  /// getType() is ok if this is known to only have one type.
  template<typename X = void>
  typename std::enable_if<has_result<X>::value, SILType>::type
  getType(unsigned i = 0) const { return ValueBase::getType(i); }

  ArrayRef<Operand> getAllOperands() const { return Operands.asArray(); }
  MutableArrayRef<Operand> getAllOperands() { return Operands.asArray(); }

  static bool classof(const ValueBase *V) {
    return V->getKind() == KIND;
  }
};

//===----------------------------------------------------------------------===//
// Allocation Instructions
//===----------------------------------------------------------------------===//

/// Abstract base class for allocation instructions, like alloc_stack, alloc_box
/// and alloc_ref, etc.
class AllocationInst : public SILInstruction {
protected:
  AllocationInst(ValueKind Kind, SILDebugLocation *DebugLoc, SILType Ty)
      : SILInstruction(Kind, DebugLoc, Ty) {}
  AllocationInst(ValueKind Kind, SILDebugLocation *DebugLoc,
                 SILTypeList *TypeList = nullptr)
      : SILInstruction(Kind, DebugLoc, TypeList) {}

public:

  static bool classof(const ValueBase *V) {
    return V->getKind() >= ValueKind::First_AllocationInst &&
      V->getKind() <= ValueKind::Last_AllocationInst;
  }
};

/// Base class for allocation/deallocation instructions where the allocation
/// can be promoted to the stack.
/// Note that IRGen can still decide to _not_ promote the allocation on the
/// stack.
class StackPromotable {

  /// If true, the allocation can be done on the stack (the final decision is
  /// in IRGen).
  bool OnStack = false;

public:
  StackPromotable(bool OnStack) : OnStack(OnStack) { }

  bool canAllocOnStack() const { return OnStack; }

  void setStackAllocatable() { OnStack = true; }
};

/// AllocStackInst - This represents the allocation of an unboxed (i.e., no
/// reference count) stack memory.  The memory is provided uninitialized.
class AllocStackInst : public AllocationInst {
  friend class SILBuilder;

  AllocStackInst(SILDebugLocation *Loc, SILType elementType, SILFunction &F);

public:

  /// getDecl - Return the underlying variable declaration associated with this
  /// allocation, or null if this is a temporary allocation.
  VarDecl *getDecl() const;

  /// getElementType - Get the type of the allocated memory (as opposed to the
  /// (second) type of the instruction itself, which will be an address type).
  SILType getElementType() const {
    return getType(1).getObjectType();
  }

  SILValue getContainerResult() const { return SILValue(this, 0); }
  SILValue getAddressResult() const { return SILValue(this, 1); }

  ArrayRef<Operand> getAllOperands() const { return {}; }
  MutableArrayRef<Operand> getAllOperands() { return {}; }

  static bool classof(const ValueBase *V) {
    return V->getKind() == ValueKind::AllocStackInst;
  }
};

/// AllocRefInst - This represents the primitive allocation of an instance
/// of a reference type. Aside from the reference count, the instance is
/// returned uninitialized.
class AllocRefInst : public AllocationInst, public StackPromotable {
  friend class SILBuilder;
  bool ObjC;

  AllocRefInst(SILDebugLocation *Loc, SILType type, SILFunction &F, bool objc,
               bool canBeOnStack);

public:

  SILType getType(unsigned i = 0) const { return ValueBase::getType(i); }

  ArrayRef<Operand> getAllOperands() const { return {}; }
  MutableArrayRef<Operand> getAllOperands() { return {}; }

  /// Whether to use Objective-C's allocation mechanism (+allocWithZone:).
  bool isObjC() const { return ObjC; }

  static bool classof(const ValueBase *V) {
    return V->getKind() == ValueKind::AllocRefInst;
  }
};

/// AllocRefDynamicInst - This represents the primitive allocation of
/// an instance of a reference type whose runtime type is provided by
/// the given metatype value. Aside from the reference count, the
/// instance is returned uninitialized.
class AllocRefDynamicInst
  : public UnaryInstructionBase<ValueKind::AllocRefDynamicInst, AllocationInst>
{
  friend class SILBuilder;

  bool ObjC;

  AllocRefDynamicInst(SILDebugLocation *DebugLoc, SILValue operand, SILType ty,
                      bool objc)
      : UnaryInstructionBase(DebugLoc, operand, ty), ObjC(objc) {}

public:

  /// Whether to use Objective-C's allocation mechanism (+allocWithZone:).
  bool isObjC() const { return ObjC; }
};

/// AllocValueBufferInst - Allocate memory in a value buffer.
class AllocValueBufferInst :
  public UnaryInstructionBase<ValueKind::AllocValueBufferInst,
                              AllocationInst> {
  friend class SILBuilder;

  AllocValueBufferInst(SILDebugLocation *DebugLoc, SILType valueType,
                       SILValue operand)
      : UnaryInstructionBase(DebugLoc, operand, valueType.getAddressType()) {}

public:

  SILType getValueType() const { return getType().getObjectType(); }
};

/// This represents the allocation of a heap box for a Swift value of some type.
/// The instruction returns two values.  The first return value is the object
/// pointer with Builtin.NativeObject type.  The second return value
/// is an address pointing to the contained element. The contained
/// element is uninitialized.
class AllocBoxInst : public AllocationInst {
  friend class SILBuilder;

  AllocBoxInst(SILDebugLocation *DebugLoc, SILType ElementType, SILFunction &F);

public:

  SILType getElementType() const {
    return getType(1).getObjectType();
  }

  SILValue getContainerResult() const { return SILValue(this, 0); }
  SILValue getAddressResult() const { return SILValue(this, 1); }

  /// getDecl - Return the underlying variable declaration associated with this
  /// allocation, or null if this is a temporary allocation.
  VarDecl *getDecl() const;

  ArrayRef<Operand> getAllOperands() const { return {}; }
  MutableArrayRef<Operand> getAllOperands() { return {}; }

  static bool classof(const ValueBase *V) {
    return V->getKind() == ValueKind::AllocBoxInst;
  }
};

/// This represents the allocation of a heap box for an existential container.
/// The instruction returns two values.  The first return value is the owner
/// pointer, which has the existential type.  The second return value
/// is an address pointing to the contained element. The contained
/// value is uninitialized.
class AllocExistentialBoxInst : public AllocationInst {
  friend class SILBuilder;

  CanType ConcreteType;
  ArrayRef<ProtocolConformance*> Conformances;

  AllocExistentialBoxInst(SILDebugLocation *DebugLoc, SILType ExistentialType,
                          CanType ConcreteType, SILType ConcreteLoweredType,
                          ArrayRef<ProtocolConformance *> Conformances,
                          SILFunction *Parent);

  static AllocExistentialBoxInst *
  create(SILDebugLocation *DebugLoc, SILType ExistentialType,
         CanType ConcreteType, SILType ConcreteLoweredType,
         ArrayRef<ProtocolConformance *> Conformances, SILFunction *Parent);

public:
  CanType getFormalConcreteType() const {
    return ConcreteType;
  }
  
  SILType getExistentialType() const {
    return getType(0);
  }

  SILType getLoweredConcreteType() const {
    return getType(1);
  }

  ArrayRef<ProtocolConformance*> getConformances() const {
    return Conformances;
  }
  
  SILValue getExistentialResult() const { return SILValue(this, 0); }
  SILValue getValueAddressResult() const { return SILValue(this, 1); }
  
  ArrayRef<Operand> getAllOperands() const { return {}; }
  MutableArrayRef<Operand> getAllOperands() { return {}; }

  static bool classof(const ValueBase *V) {
    return V->getKind() == ValueKind::AllocExistentialBoxInst;
  }
};

void *allocateApplyInst(SILFunction &F, size_t size, size_t align);
class PartialApplyInst;

/// ApplyInstBase - An abstract class for different kinds of function
/// application.
template <class Impl, class Base,
          bool IsFullApply = !std::is_same<Impl, PartialApplyInst>::value>
class ApplyInstBase;

// The partial specialization for non-full applies.  Note that the
// partial specialization for full applies inherits from this.
template <class Impl, class Base>
class ApplyInstBase<Impl, Base, false> : public Base {
  enum {
    Callee
  };

  /// The type of the callee with our substitutions applied.
  SILType SubstCalleeType;

  /// The number of tail-allocated substitutions, allocated after the operand
  /// list's tail allocation.
  unsigned NumSubstitutions;

  /// Used for apply_inst instructions: true if the called function has an
  /// error result but is not actually throwing.
  bool NonThrowing;

  /// The fixed operand is the callee;  the rest are arguments.
  TailAllocatedOperandList<1> Operands;

  Substitution *getSubstitutionsStorage() {
    return reinterpret_cast<Substitution*>(Operands.asArray().end());
  }

  const Substitution *getSubstitutionsStorage() const {
    return reinterpret_cast<const Substitution*>(Operands.asArray().end());
  }

protected:
  template <class... As>
  ApplyInstBase(ValueKind kind, SILDebugLocation *DebugLoc, SILValue callee,
                SILType substCalleeType, ArrayRef<Substitution> substitutions,
                ArrayRef<SILValue> args, As... baseArgs)
      : Base(kind, DebugLoc, baseArgs...), SubstCalleeType(substCalleeType),
        NumSubstitutions(substitutions.size()), NonThrowing(false),
        Operands(this, args, callee) {
    static_assert(sizeof(Impl) == sizeof(*this),
        "subclass has extra storage, cannot use TailAllocatedOperandList");
    memcpy(getSubstitutionsStorage(), substitutions.begin(),
           sizeof(substitutions[0]) * substitutions.size());
  }

  static void *allocate(SILFunction &F,
                        ArrayRef<Substitution> substitutions,
                        ArrayRef<SILValue> args) {
    return allocateApplyInst(F,
                    sizeof(Impl) +
                    decltype(Operands)::getExtraSize(args.size()) +
                    sizeof(substitutions[0]) * substitutions.size(),
                             alignof(Impl));
  }

  void setNonThrowing(bool isNonThrowing) { NonThrowing = isNonThrowing; }
  
  bool isNonThrowingApply() const { return NonThrowing; }
  
public:
  // The operand number of the first argument.
  static unsigned getArgumentOperandNumber() { return 1; }

  SILValue getCallee() const { return Operands[Callee].get(); }

  // Gets the referenced function if the callee is a function_ref instruction.
  SILFunction *getCalleeFunction() const {
    if (auto *FRI = dyn_cast<FunctionRefInst>(getCallee()))
      return FRI->getReferencedFunction();
    return nullptr;
  }

  // Get the type of the callee without the applied substitutions.
  CanSILFunctionType getOrigCalleeType() const {
    return getCallee().getType().template castTo<SILFunctionType>();
  }

  // Get the type of the callee with the applied substitutions.
  CanSILFunctionType getSubstCalleeType() const {
    return SubstCalleeType.castTo<SILFunctionType>();
  }
  SILType getSubstCalleeSILType() const {
    return SubstCalleeType;
  }

  SILResultInfo getSubstCalleeResultInfo() const {
    return getSubstCalleeType()->getResult();
  }
  bool hasResultConvention(ResultConvention Conv) const {
    return getSubstCalleeResultInfo().getConvention() == Conv;
  }

  bool isCalleeThin() const {
    auto Rep = getSubstCalleeType()->getRepresentation();
    return Rep == FunctionType::Representation::Thin;
  }

  /// True if this application has generic substitutions.
  bool hasSubstitutions() const { return NumSubstitutions != 0; }

  /// The substitutions used to bind the generic arguments of this function.
  MutableArrayRef<Substitution> getSubstitutions() {
    return {getSubstitutionsStorage(), NumSubstitutions};
  }

  ArrayRef<Substitution> getSubstitutions() const {
    return {getSubstitutionsStorage(), NumSubstitutions};
  }

  /// The arguments passed to this instruction.
  MutableArrayRef<Operand> getArgumentOperands() {
    return Operands.getDynamicAsArray();
  }

  ArrayRef<Operand> getArgumentOperands() const {
    return Operands.getDynamicAsArray();
  }

  /// The arguments passed to this instruction.
  OperandValueArrayRef getArguments() const {
    return Operands.getDynamicValuesAsArray();
  }

  /// Returns the number of arguments for this partial apply.
  unsigned getNumArguments() const { return getArguments().size(); }

  Operand &getArgumentRef(unsigned i) {
    return Operands.getDynamicAsArray()[i];
  }

  /// Return the ith argument passed to this instruction.
  SILValue getArgument(unsigned i) const { return getArguments()[i]; }

  // Set the ith argument of this instruction.
  void setArgument(unsigned i, SILValue V) {
    return getArgumentOperands()[i].set(V);
  }

  ArrayRef<Operand> getAllOperands() const { return Operands.asArray(); }

  MutableArrayRef<Operand> getAllOperands() { return Operands.asArray(); }
};

/// Given the callee operand of an apply or try_apply instruction,
/// does it have the given semantics?
bool doesApplyCalleeHaveSemantics(SILValue callee, StringRef semantics);

// The partial specialization of ApplyInstBase for full applications.
// Adds some methods relating to 'self' and to result types that don't
// make sense for partial applications.
template <class Impl, class Base>
class ApplyInstBase<Impl, Base, true>
  : public ApplyInstBase<Impl, Base, false> {
  using super = ApplyInstBase<Impl, Base, false>;
protected:
  template <class... As>
  ApplyInstBase(As &&...args)
    : ApplyInstBase<Impl,Base,false>(std::forward<As>(args)...) {}

public:
  using super::getCallee;
  using super::getSubstCalleeType;
  using super::hasSubstitutions;
  using super::getSubstitutions;
  using super::getNumArguments;
  using super::getArgument;
  using super::getArguments;
  using super::getArgumentOperands;

  /// The collection of following routines wrap the representation difference in
  /// between the self substitution being first, but the self parameter of a
  /// function being last.
  ///
  /// The hope is that this will prevent any future bugs from coming up related
  /// to this.
  ///
  /// Self is always the last parameter, but self subtitutions are always
  /// first. The reason to add this method is to wrap that dichotomy to reduce
  /// errors.
  ///
  /// FIXME: Could this be standardized? It has and will lead to bugs. IMHO.
  SILValue getSelfArgument() const {
    assert(hasSelfArgument() && "Must have a self argument");
    assert(getNumArguments() && "Should only be called when Callee has "
           "arguments.");
    return getArgument(getNumArguments()-1);
  }

  Operand &getSelfArgumentOperand() {
    assert(hasSelfArgument() && "Must have a self argument");
    assert(getNumArguments() && "Should only be called when Callee has "
           "arguments.");
    return getArgumentOperands()[getNumArguments()-1];
  }

  void setSelfArgument(SILValue V) {
    assert(hasSelfArgument() && "Must have a self argument");
    assert(getNumArguments() && "Should only be called when Callee has "
                                      "arguments.");
    getArgumentOperands()[getNumArguments() - 1].set(V);
  }

  OperandValueArrayRef getArgumentsWithoutSelf() const {
    assert(hasSelfArgument() && "Must have a self argument");
    assert(getNumArguments() && "Should only be called when Callee has "
           "at least a self parameter.");
    assert(hasSubstitutions() && "Should only be called when Callee has "
           "substitutions.");
    ArrayRef<Operand> ops = this->getArgumentOperands();
    ArrayRef<Operand> opsWithoutSelf = ArrayRef<Operand>(&ops[0],
                                                         ops.size()-1);
    return OperandValueArrayRef(opsWithoutSelf);
  }

  Substitution getSelfSubstitution() const {
    assert(getNumArguments() && "Should only be called when Callee has "
           "at least a self parameter.");
    assert(hasSubstitutions() && "Should only be called when Callee has "
           "substitutions.");
    return getSubstitutions()[0];
  }

  ArrayRef<Substitution> getSubstitutionsWithoutSelfSubstitution() const {
    assert(getNumArguments() && "Should only be called when Callee has "
           "at least a self parameter.");
    assert(hasSubstitutions() && "Should only be called when Callee has "
           "substitutions.");
    return getSubstitutions().slice(1);
  }

  bool hasIndirectResult() const {
    return getSubstCalleeType()->hasIndirectResult();
  }

  bool hasSelfArgument() const {
    return getSubstCalleeType()->hasSelfParam();
  }

  bool hasGuaranteedSelfArgument() const {
    auto C = getSubstCalleeType()->getSelfParameter().getConvention();
    return C == ParameterConvention::Direct_Guaranteed;
  }

  SILValue getIndirectResult() const {
    assert(hasIndirectResult() && "apply inst does not have indirect result!");
    return getArguments().front();
  }

  OperandValueArrayRef getArgumentsWithoutIndirectResult() const {
    if (hasIndirectResult())
      return getArguments().slice(1);
    return getArguments();
  }

  bool hasSemantics(StringRef semanticsString) const {
    return doesApplyCalleeHaveSemantics(getCallee(), semanticsString);
  }
};

/// ApplyInst - Represents the full application of a function value.
class ApplyInst : public ApplyInstBase<ApplyInst, SILInstruction> {
  friend class SILBuilder;

  ApplyInst(SILDebugLocation *DebugLoc, SILValue Callee,
            SILType SubstCalleeType, SILType ReturnType,
            ArrayRef<Substitution> Substitutions, ArrayRef<SILValue> Args,
            bool isNonThrowing);

  static ApplyInst *create(SILDebugLocation *DebugLoc, SILValue Callee,
                           SILType SubstCalleeType, SILType ReturnType,
                           ArrayRef<Substitution> Substitutions,
                           ArrayRef<SILValue> Args, bool isNonThrowing,
                           SILFunction &F);

public:
  /// getType() is ok since this is known to only have one type.
  SILType getType(unsigned i = 0) const { return ValueBase::getType(i); }

  static bool classof(const ValueBase *V) {
    return V->getKind() == ValueKind::ApplyInst;
  }
  
  /// Returns true if the called function has an error result but is not actully
  /// throwing an error.
  bool isNonThrowing() const {
    return isNonThrowingApply();
  }
};

/// PartialApplyInst - Represents the creation of a closure object by partial
/// application of a function value.
class PartialApplyInst
    : public ApplyInstBase<PartialApplyInst, SILInstruction> {
  friend class SILBuilder;

  PartialApplyInst(SILDebugLocation *DebugLoc, SILValue Callee,
                   SILType SubstCalleeType,
                   ArrayRef<Substitution> Substitutions,
                   ArrayRef<SILValue> Args, SILType ClosureType);

  static PartialApplyInst *create(SILDebugLocation *DebugLoc, SILValue Callee,
                                  SILType SubstCalleeType,
                                  ArrayRef<Substitution> Substitutions,
                                  ArrayRef<SILValue> Args, SILType ClosureType,
                                  SILFunction &F);

public:
  /// getType() is ok since this is known to only have one type.
  SILType getType(unsigned i = 0) const { return ValueBase::getType(i); }


  /// Return the ast level function type of this partial apply.
  CanSILFunctionType getFunctionType() const {
    return getType().castTo<SILFunctionType>();
  }

  static bool classof(const ValueBase *V) {
    return V->getKind() == ValueKind::PartialApplyInst;
  }
};

//===----------------------------------------------------------------------===//
// Literal instructions.
//===----------------------------------------------------------------------===//

/// Abstract base class for literal instructions.
class LiteralInst : public SILInstruction {
protected:
  LiteralInst(ValueKind Kind, SILDebugLocation *DebugLoc, SILType Ty)
      : SILInstruction(Kind, DebugLoc, Ty) {}

public:

  static bool classof(const ValueBase *V) {
    return V->getKind() >= ValueKind::First_LiteralInst &&
      V->getKind() <= ValueKind::Last_LiteralInst;
  }
};

/// FunctionRefInst - Represents a reference to a SIL function.
class FunctionRefInst : public LiteralInst {
  friend class SILBuilder;

  SILFunction *Function;
  /// Construct a FunctionRefInst.
  ///
  /// \param DebugLoc  The location of the reference.
  /// \param F    The function being referenced.
  FunctionRefInst(SILDebugLocation *DebugLoc, SILFunction *F);

public:
  ~FunctionRefInst();

  /// Return the referenced function.
  SILFunction *getReferencedFunction() const { return Function; }

  void dropReferencedFunction();

  /// getType() is ok since this is known to only have one type.
  /// The type is always a lowered function type.
  SILType getType(unsigned i = 0) const { return ValueBase::getType(i); }

  CanSILFunctionType getFunctionType() const {
    return getType().castTo<SILFunctionType>();
  }

  ArrayRef<Operand> getAllOperands() const { return {}; }
  MutableArrayRef<Operand> getAllOperands() { return {}; }

  static bool classof(const ValueBase *V) {
    return V->getKind() == ValueKind::FunctionRefInst;
  }
};

/// Represents an invocation of builtin functionality provided by the code
/// generator.
class BuiltinInst : public SILInstruction {
  friend class SILBuilder;

  /// The name of the builtin to invoke.
  Identifier Name;
  
  /// The number of tail-allocated substitutions, allocated after the operand
  /// list's tail allocation.
  unsigned NumSubstitutions;
  
  /// The value arguments to the builtin.
  TailAllocatedOperandList<0> Operands;
  
  Substitution *getSubstitutionsStorage() {
    return reinterpret_cast<Substitution*>(Operands.asArray().end());
  }
  const Substitution *getSubstitutionsStorage() const {
    return reinterpret_cast<const Substitution*>(Operands.asArray().end());
  }

  BuiltinInst(SILDebugLocation *DebugLoc, Identifier Name, SILType ReturnType,
              ArrayRef<Substitution> Substitutions, ArrayRef<SILValue> Args);

  static BuiltinInst *create(SILDebugLocation *DebugLoc, Identifier Name,
                             SILType ReturnType,
                             ArrayRef<Substitution> Substitutions,
                             ArrayRef<SILValue> Args, SILFunction &F);

public:
  /// Return the name of the builtin operation.
  Identifier getName() const { return Name; }
  void setName(Identifier I) { Name = I; }
  
  /// Currently all builtins have one result, so getType() is OK.
  /// We may want to change that eventually.
  SILType getType(unsigned i = 0) const { return ValueBase::getType(i); }

  /// \brief Looks up the llvm intrinsic ID and type for the builtin function.
  ///
  /// \returns Returns llvm::Intrinsic::not_intrinsic if the function is not an
  /// intrinsic. The particular intrinsic functions which correspond to the
  /// returned value are defined in llvm/Intrinsics.h.
  const IntrinsicInfo &getIntrinsicInfo() const;
  
  /// \brief Looks up the lazily cached identification for the builtin function.
  const BuiltinInfo &getBuiltinInfo() const;

  /// \brief Looks up the llvm intrinsic ID of this builtin. Returns None if
  /// this is not an intrinsic.
  llvm::Optional<llvm::Intrinsic::ID> getIntrinsicID() const {
    auto I = getIntrinsicInfo();
    if (I.ID == llvm::Intrinsic::not_intrinsic)
      return None;
    return I.ID;
  }

  /// \brief Looks up the BuiltinKind of this builtin. Returns None if this is
  /// not a builtin.
  llvm::Optional<BuiltinValueKind> getBuiltinKind() const {
    auto I = getBuiltinInfo();
    if (I.ID == BuiltinValueKind::None)
      return None;
    return I.ID;
  }

  /// True if this builtin application has substitutions, which represent type
  /// parameters to the builtin.
  bool hasSubstitutions() const {
    return NumSubstitutions != 0;
  }

  /// Return the type parameters to the builtin.
  ArrayRef<Substitution> getSubstitutions() const {
    return {getSubstitutionsStorage(), NumSubstitutions};
  }
  /// Return the type parameters to the builtin.
  MutableArrayRef<Substitution> getSubstitutions() {
    return {getSubstitutionsStorage(), NumSubstitutions};
  }
  
  /// The arguments to the builtin.
  ArrayRef<Operand> getAllOperands() const {
    return Operands.asArray();
  }
  /// The arguments to the builtin.
  MutableArrayRef<Operand> getAllOperands() {
    return Operands.asArray();
  }
  /// The arguments to the builtin.
  OperandValueArrayRef getArguments() const {
    return Operands.asValueArray();
  }
  
  static bool classof(const ValueBase *V) {
    return V->getKind() == ValueKind::BuiltinInst;
  }
};
  
/// Gives the address of a SIL global variable.
class GlobalAddrInst : public LiteralInst {
  friend class SILBuilder;

  SILGlobalVariable *Global;

  GlobalAddrInst(SILDebugLocation *DebugLoc, SILGlobalVariable *Global);

public:
  // FIXME: This constructor should be private but is currently used
  //        in the SILParser.

  /// Create a placeholder instruction with an unset global reference.
  GlobalAddrInst(SILDebugLocation *DebugLoc, SILType Ty);

  /// Return the referenced global variable.
  SILGlobalVariable *getReferencedGlobal() const { return Global; }
  
  void setReferencedGlobal(SILGlobalVariable *v) { Global = v; }

  /// getType() is ok since this is known to only have one type.
  SILType getType(unsigned i = 0) const { return ValueBase::getType(i); }

  ArrayRef<Operand> getAllOperands() const { return {}; }
  MutableArrayRef<Operand> getAllOperands() { return {}; }

  static bool classof(const ValueBase *V) {
    return V->getKind() == ValueKind::GlobalAddrInst;
  }
};

/// IntegerLiteralInst - Encapsulates an integer constant, as defined originally
/// by an IntegerLiteralExpr.
class IntegerLiteralInst : public LiteralInst {
  friend class SILBuilder;

  unsigned numBits;

  IntegerLiteralInst(SILDebugLocation *Loc, SILType Ty, const APInt &Value);

  static IntegerLiteralInst *create(IntegerLiteralExpr *E,
                                    SILDebugLocation *Loc, SILFunction &B);
  static IntegerLiteralInst *create(SILDebugLocation *Loc, SILType Ty,
                                    intmax_t Value, SILFunction &B);
  static IntegerLiteralInst *create(SILDebugLocation *Loc, SILType Ty,
                                    const APInt &Value, SILFunction &B);

public:
  /// getValue - Return the APInt for the underlying integer literal.
  APInt getValue() const;

  /// getType() is ok since this is known to only have one type.
  SILType getType(unsigned i = 0) const { return ValueBase::getType(i); }

  ArrayRef<Operand> getAllOperands() const { return {}; }
  MutableArrayRef<Operand> getAllOperands() { return {}; }

  static bool classof(const ValueBase *V) {
    return V->getKind() == ValueKind::IntegerLiteralInst;
  }
};

/// FloatLiteralInst - Encapsulates a floating point constant, as defined
/// originally by a FloatLiteralExpr.
class FloatLiteralInst : public LiteralInst {
  friend class SILBuilder;

  unsigned numBits;

  FloatLiteralInst(SILDebugLocation *Loc, SILType Ty, const APInt &Bits);

  static FloatLiteralInst *create(FloatLiteralExpr *E, SILDebugLocation *Loc,
                                  SILFunction &B);
  static FloatLiteralInst *create(SILDebugLocation *Loc, SILType Ty,
                                  const APFloat &Value, SILFunction &B);

public:
  /// \brief Return the APFloat for the underlying FP literal.
  APFloat getValue() const;

  /// \brief Return the bitcast representation of the FP literal as an APInt.
  APInt getBits() const;

  /// getType() is ok since this is known to only have one type.
  SILType getType(unsigned i = 0) const { return ValueBase::getType(i); }

  ArrayRef<Operand> getAllOperands() const { return {}; }
  MutableArrayRef<Operand> getAllOperands() { return {}; }

  static bool classof(const ValueBase *V) {
    return V->getKind() == ValueKind::FloatLiteralInst;
  }
};

/// StringLiteralInst - Encapsulates a string constant, as defined originally by
/// a StringLiteralExpr.  This produces the address of the string data as a
/// Builtin.RawPointer.
class StringLiteralInst : public LiteralInst {
  friend class SILBuilder;

public:
  enum class Encoding {
    UTF8,
    UTF16
  };

private:
  unsigned Length;
  Encoding TheEncoding;

  StringLiteralInst(SILDebugLocation *DebugLoc, StringRef text,
                    Encoding encoding, SILType ty);

  static StringLiteralInst *create(SILDebugLocation *DebugLoc, StringRef Text,
                                   Encoding encoding, SILFunction &F);

public:
  /// getValue - Return the string data for the literal, in UTF-8.
  StringRef getValue() const {
    return {reinterpret_cast<char const *>(this + 1), Length};
  }

  /// getEncoding - Return the desired encoding of the text.
  Encoding getEncoding() const { return TheEncoding; }

  /// getCodeUnitCount - Return encoding-based length of the string
  /// literal in code units.
  uint64_t getCodeUnitCount();

  ArrayRef<Operand> getAllOperands() const { return {}; }
  MutableArrayRef<Operand> getAllOperands() { return {}; }

  static bool classof(const ValueBase *V) {
    return V->getKind() == ValueKind::StringLiteralInst;
  }
};

/// StringLiteralInst::Encoding hashes to its underlying integer representation.
static inline llvm::hash_code hash_value(StringLiteralInst::Encoding E) {
  return llvm::hash_value(size_t(E));
}

/// LoadInst - Represents a load from a memory location.
class LoadInst
  : public UnaryInstructionBase<ValueKind::LoadInst>
{
  friend class SILBuilder;

  /// Constructs a LoadInst.
  ///
  /// \param DebugLoc The location of the expression that caused the load.
  ///
  /// \param LValue The SILValue representing the lvalue (address) to
  ///        use for the load.
  LoadInst(SILDebugLocation *DebugLoc, SILValue LValue)
      : UnaryInstructionBase(DebugLoc, LValue,
                             LValue.getType().getObjectType()) {}
};

/// StoreInst - Represents a store from a memory location.
class StoreInst : public SILInstruction {
  friend class SILBuilder;

private:
  FixedOperandList<2> Operands;

  StoreInst(SILDebugLocation *DebugLoc, SILValue Src, SILValue Dest);

public:
  enum {
    /// the value being stored
    Src,
    /// the lvalue being stored to
    Dest
  };

  SILValue getSrc() const { return Operands[Src].get(); }
  SILValue getDest() const { return Operands[Dest].get(); }

  ArrayRef<Operand> getAllOperands() const { return Operands.asArray(); }
  MutableArrayRef<Operand> getAllOperands() { return Operands.asArray(); }

  static bool classof(const ValueBase *V) {
    return V->getKind() == ValueKind::StoreInst;
  }
};

/// AssignInst - Represents an abstract assignment to a memory location, which
/// may either be an initialization or a store sequence.  This is only valid in
/// Raw SIL.
class AssignInst : public SILInstruction {
  friend class SILBuilder;

  enum {
    /// the value being stored
    Src,
    /// the lvalue being stored to
    Dest
  };
  FixedOperandList<2> Operands;

  AssignInst(SILDebugLocation *DebugLoc, SILValue Src, SILValue Dest);

public:

  SILValue getSrc() const { return Operands[Src].get(); }
  SILValue getDest() const { return Operands[Dest].get(); }

  bool isUnownedAssign() const {
    return getDest().getType().getObjectType().is<UnownedStorageType>();
  }

  ArrayRef<Operand> getAllOperands() const { return Operands.asArray(); }
  MutableArrayRef<Operand> getAllOperands() { return Operands.asArray(); }

  static bool classof(const ValueBase *V) {
    return V->getKind() == ValueKind::AssignInst;
  }
};

/// MarkUninitializedInst - Indicates that a memory location is uninitialized at
/// this point and needs to be initialized by the end of the function and before
/// any escape point for this instruction.  This is only valid in Raw SIL.
class MarkUninitializedInst
  : public UnaryInstructionBase<ValueKind::MarkUninitializedInst> {
  friend class SILBuilder;

public:
  // This enum captures what the mark_uninitialized instruction is designating.
  enum Kind {
    /// Var designates the start of a normal variable live range.
    Var,

    /// RootSelf designates "self" in a struct, enum, or root class.
    RootSelf,

    /// DerivedSelf designates "self" in a derived (non-root) class.
    DerivedSelf,

    /// DerivedSelfOnly designates "self" in a derived (non-root)
    /// class whose stored properties have already been initialized.
    DerivedSelfOnly,

    /// DelegatingSelf designates "self" on a struct, enum, or class
    /// in a delegating constructor (one that calls self.init).
    DelegatingSelf,
  };
private:
  Kind ThisKind;

  MarkUninitializedInst(SILDebugLocation *DebugLoc, SILValue Address, Kind K)
      : UnaryInstructionBase(DebugLoc, Address, Address.getType()),
        ThisKind(K) {}

public:

  Kind getKind() const { return ThisKind; }

  bool isVar() const { return ThisKind == Var; }
  bool isRootSelf() const {
    return ThisKind == RootSelf;
  }
  bool isDerivedClassSelf() const {
    return ThisKind == DerivedSelf;
  }
  bool isDerivedClassSelfOnly() const {
    return ThisKind == DerivedSelfOnly;
  }
  bool isDelegatingSelf() const {
    return ThisKind == DelegatingSelf;
  }
};

/// MarkFunctionEscape - Represents the escape point of set of variables due to
/// a function definition which uses the variables.  This is only valid in Raw
/// SIL.
class MarkFunctionEscapeInst : public SILInstruction {
  friend class SILBuilder;

  TailAllocatedOperandList<0> Operands;

  /// Private constructor.  Because this is variadic, object creation goes
  /// through 'create()'.
  MarkFunctionEscapeInst(SILDebugLocation *DebugLoc,
                         ArrayRef<SILValue> Elements);

  /// Construct a MarkFunctionEscapeInst.
  static MarkFunctionEscapeInst *create(SILDebugLocation *DebugLoc,
                                        ArrayRef<SILValue> Elements,
                                        SILFunction &F);

public:
  /// The elements referenced by this instruction.
  MutableArrayRef<Operand> getElementOperands() {
    return Operands.getDynamicAsArray();
  }

  /// The elements referenced by this instruction.
  OperandValueArrayRef getElements() const {
    return Operands.getDynamicValuesAsArray();
  }

  ArrayRef<Operand> getAllOperands() const { return Operands.asArray(); }
  MutableArrayRef<Operand> getAllOperands() { return Operands.asArray(); }

  static bool classof(const ValueBase *V) {
    return V->getKind() == ValueKind::MarkFunctionEscapeInst;
  }
};

/// Define the start or update to a symbolic variable value (for loadable
/// types).
class DebugValueInst : public UnaryInstructionBase<ValueKind::DebugValueInst> {
  friend class SILBuilder;

  DebugValueInst(SILDebugLocation *DebugLoc, SILValue Operand)
      : UnaryInstructionBase(DebugLoc, Operand) {}

public:
  /// getDecl - Return the underlying variable declaration that this denotes,
  /// or null if we don't have one.
  VarDecl *getDecl() const;
};

/// Define the start or update to a symbolic variable value (for address-only
/// types) .
class DebugValueAddrInst
  : public UnaryInstructionBase<ValueKind::DebugValueAddrInst> {
  friend class SILBuilder;

  DebugValueAddrInst(SILDebugLocation *DebugLoc, SILValue Operand)
      : UnaryInstructionBase(DebugLoc, Operand) {}

public:
  /// getDecl - Return the underlying variable declaration that this denotes,
  /// or null if we don't have one.
  VarDecl *getDecl() const;
};



/// Represents a load from a @weak memory location.
class LoadWeakInst
  : public UnaryInstructionBase<ValueKind::LoadWeakInst>
{
  friend class SILBuilder;

  static SILType getResultType(SILType operandTy) {
    assert(operandTy.isAddress() && "loading from non-address operand?");
    auto refType = cast<ReferenceStorageType>(operandTy.getSwiftRValueType());
    return SILType::getPrimitiveObjectType(refType.getReferentType());
  }

  unsigned IsTake : 1; // FIXME: pack this somewhere

  /// \param DebugLoc The location of the expression that caused the load.
  /// \param lvalue The SILValue representing the address to
  ///        use for the load.
  LoadWeakInst(SILDebugLocation *DebugLoc, SILValue lvalue, IsTake_t isTake)
      : UnaryInstructionBase(DebugLoc, lvalue, getResultType(lvalue.getType())),
        IsTake(unsigned(isTake)) {}

public:
  IsTake_t isTake() const { return IsTake_t(IsTake); }
};

/// Represents a store to a @weak memory location.
class StoreWeakInst : public SILInstruction {
  friend class SILBuilder;

  enum { Src, Dest };
  FixedOperandList<2> Operands;
  unsigned IsInitializationOfDest : 1; // FIXME: pack this somewhere
  StoreWeakInst(SILDebugLocation *DebugLoc, SILValue src, SILValue dest,
                IsInitialization_t isInit);

public:
  SILValue getSrc() const { return Operands[Src].get(); }
  SILValue getDest() const { return Operands[Dest].get(); }

  IsInitialization_t isInitializationOfDest() const {
    return IsInitialization_t(IsInitializationOfDest);
  }
  void setIsInitializationOfDest(IsInitialization_t I) {
    IsInitializationOfDest = (bool)I;
  }

  ArrayRef<Operand> getAllOperands() const { return Operands.asArray(); }
  MutableArrayRef<Operand> getAllOperands() { return Operands.asArray(); }

  static bool classof(const ValueBase *V) {
    return V->getKind() == ValueKind::StoreWeakInst;
  }
};

/// CopyAddrInst - Represents a copy from one memory location to another. This
/// is similar to:
///   %1 = load %src
///   store %1 to %dest
/// but a copy instruction must be used for address-only types.
class CopyAddrInst : public SILInstruction {
  friend class SILBuilder;

public:
  enum {
    /// The lvalue being loaded from.
    Src,

    /// The lvalue being stored to.
    Dest
  };

private:
  // FIXME: compress storage

  /// IsTakeOfSrc - True if ownership will be taken from the value at the source
  /// memory location.
  unsigned IsTakeOfSrc : 1;

  /// IsInitializationOfDest - True if this is the initialization of the
  /// uninitialized destination memory location.
  unsigned IsInitializationOfDest : 1;

  FixedOperandList<2> Operands;

  CopyAddrInst(SILDebugLocation *DebugLoc, SILValue Src, SILValue Dest,
               IsTake_t isTakeOfSrc, IsInitialization_t isInitializationOfDest);

public:
  SILValue getSrc() const { return Operands[Src].get(); }
  SILValue getDest() const { return Operands[Dest].get(); }

  void setSrc(SILValue V) { Operands[Src].set(V); }
  void setDest(SILValue V) { Operands[Dest].set(V); }

  IsTake_t isTakeOfSrc() const { return IsTake_t(IsTakeOfSrc); }
  IsInitialization_t isInitializationOfDest() const {
    return IsInitialization_t(IsInitializationOfDest);
  }

  void setIsTakeOfSrc(IsTake_t T) {
    IsTakeOfSrc = (bool)T;
  }
  void setIsInitializationOfDest(IsInitialization_t I) {
    IsInitializationOfDest = (bool)I;
  }

  ArrayRef<Operand> getAllOperands() const { return Operands.asArray(); }
  MutableArrayRef<Operand> getAllOperands() { return Operands.asArray(); }

  static bool classof(const ValueBase *V) {
    return V->getKind() == ValueKind::CopyAddrInst;
  }
};

/// ConversionInst - Abstract class representing instructions that convert
/// values.
///
class ConversionInst : public SILInstruction {
protected:
  ConversionInst(ValueKind Kind, SILDebugLocation *DebugLoc, SILType Ty)
      : SILInstruction(Kind, DebugLoc, Ty) {}

public:
  /// All conversion instructions return a single result.
  SILType getType(unsigned i = 0) const { return ValueBase::getType(i); }

  /// All conversion instructions take the converted value, whose reference
  /// identity is expected to be preserved through the conversion chain, as their
  /// first operand. Some instructions may take additional operands that do not
  /// affect the reference identity.
  SILValue getConverted() const { return getOperand(0); }
  
  static bool classof(const ValueBase *V) {
    return V->getKind() >= ValueKind::First_ConversionInst &&
      V->getKind() <= ValueKind::Last_ConversionInst;
  }
};

/// ConvertFunctionInst - Change the type of a function value without
/// affecting how it will codegen.
class ConvertFunctionInst
  : public UnaryInstructionBase<ValueKind::ConvertFunctionInst, ConversionInst>
{
  friend class SILBuilder;

  ConvertFunctionInst(SILDebugLocation *DebugLoc, SILValue Operand, SILType Ty)
      : UnaryInstructionBase(DebugLoc, Operand, Ty) {}
};

/// ThinFunctionToPointerInst - Convert a thin function pointer to a
/// Builtin.RawPointer.
class ThinFunctionToPointerInst
  : public UnaryInstructionBase<ValueKind::ThinFunctionToPointerInst,
                                ConversionInst>
{
  friend class SILBuilder;

  ThinFunctionToPointerInst(SILDebugLocation *DebugLoc, SILValue operand,
                            SILType ty)
      : UnaryInstructionBase(DebugLoc, operand, ty) {}
};

/// PointerToThinFunctionInst - Convert a Builtin.RawPointer to a thin
/// function pointer.
class PointerToThinFunctionInst
  : public UnaryInstructionBase<ValueKind::PointerToThinFunctionInst,
                                ConversionInst>
{
  friend class SILBuilder;

  PointerToThinFunctionInst(SILDebugLocation *DebugLoc, SILValue operand,
                            SILType ty)
      : UnaryInstructionBase(DebugLoc, operand, ty) {}
};

/// UpcastInst - Perform a conversion of a class instance to a supertype.
class UpcastInst
  : public UnaryInstructionBase<ValueKind::UpcastInst, ConversionInst>
{
  friend class SILBuilder;

  UpcastInst(SILDebugLocation *DebugLoc, SILValue Operand, SILType Ty)
      : UnaryInstructionBase(DebugLoc, Operand, Ty) {}
};

/// AddressToPointerInst - Convert a SIL address to a Builtin.RawPointer value.
class AddressToPointerInst
  : public UnaryInstructionBase<ValueKind::AddressToPointerInst,
                                ConversionInst>
{
  friend class SILBuilder;

  AddressToPointerInst(SILDebugLocation *DebugLoc, SILValue Operand, SILType Ty)
      : UnaryInstructionBase(DebugLoc, Operand, Ty) {}
};

/// PointerToAddressInst - Convert a Builtin.RawPointer value to a SIL address.
class PointerToAddressInst
  : public UnaryInstructionBase<ValueKind::PointerToAddressInst, ConversionInst>
{
  friend class SILBuilder;

  PointerToAddressInst(SILDebugLocation *DebugLoc, SILValue Operand, SILType Ty)
      : UnaryInstructionBase(DebugLoc, Operand, Ty) {}
};

/// Convert a heap object reference to a different type without any runtime
/// checks.
class UncheckedRefCastInst
  : public UnaryInstructionBase<ValueKind::UncheckedRefCastInst,
                                ConversionInst>
{
  friend class SILBuilder;

  UncheckedRefCastInst(SILDebugLocation *DebugLoc, SILValue Operand, SILType Ty)
      : UnaryInstructionBase(DebugLoc, Operand, Ty) {}
};

/// Converts a heap object reference to a different type without any runtime
/// checks. This is a variant of UncheckedRefCast that works on address types,
/// thus encapsulates an implicit load and take of the reference followed by a
/// store and initialization of a new reference.
class UncheckedRefCastAddrInst : public SILInstruction {
public:
  enum {
    /// the value being stored
    Src,
    /// the lvalue being stored to
    Dest
  };

private:
  FixedOperandList<2> Operands;
  CanType SourceType;
  CanType TargetType;
public:
  UncheckedRefCastAddrInst(SILDebugLocation *Loc, SILValue src, CanType srcType,
                           SILValue dest, CanType targetType);

  CastConsumptionKind getConsumptionKind() const {
    return CastConsumptionKind::TakeAlways;
  }

  SILValue getSrc() const { return Operands[Src].get(); }
  SILValue getDest() const { return Operands[Dest].get(); }

  /// Returns the formal type of the source value.
  CanType getSourceType() const { return SourceType; }

  /// Returns the formal target type.
  CanType getTargetType() const { return TargetType; }

  ArrayRef<Operand> getAllOperands() const { return Operands.asArray(); }
  MutableArrayRef<Operand> getAllOperands() { return Operands.asArray(); }

  static bool classof(const ValueBase *V) {
    return V->getKind() == ValueKind::UncheckedRefCastAddrInst;
  }
};

class UncheckedAddrCastInst
  : public UnaryInstructionBase<ValueKind::UncheckedAddrCastInst,
                                ConversionInst>
{
  friend class SILBuilder;

  UncheckedAddrCastInst(SILDebugLocation *DebugLoc, SILValue Operand,
                        SILType Ty)
      : UnaryInstructionBase(DebugLoc, Operand, Ty) {}
};

/// Convert a value's binary representation to a trivial type of the same size.
class UncheckedTrivialBitCastInst
  : public UnaryInstructionBase<ValueKind::UncheckedTrivialBitCastInst,
                                ConversionInst>
{
  friend class SILBuilder;

  UncheckedTrivialBitCastInst(SILDebugLocation *DebugLoc, SILValue Operand,
                              SILType Ty)
      : UnaryInstructionBase(DebugLoc, Operand, Ty) {}
};
  
/// Bitwise copy a value into another value of the same size or smaller.
class UncheckedBitwiseCastInst
  : public UnaryInstructionBase<ValueKind::UncheckedBitwiseCastInst,
                                ConversionInst>
{
  friend class SILBuilder;

  UncheckedBitwiseCastInst(SILDebugLocation *DebugLoc, SILValue Operand,
                           SILType Ty)
      : UnaryInstructionBase(DebugLoc, Operand, Ty) {}
};

/// Build a Builtin.BridgeObject from a heap object reference by bitwise-or-ing
/// in bits from a word.
class RefToBridgeObjectInst : public ConversionInst {
  friend class SILBuilder;

  FixedOperandList<2> Operands;
  RefToBridgeObjectInst(SILDebugLocation *DebugLoc, SILValue ConvertedValue,
                        SILValue MaskValue, SILType BridgeObjectTy)
      : ConversionInst(ValueKind::RefToBridgeObjectInst, DebugLoc,
                       BridgeObjectTy),
        Operands(this, ConvertedValue, MaskValue) {}

public:
  
  SILValue getBitsOperand() const { return Operands[1].get(); }
  
  ArrayRef<Operand> getAllOperands() const { return Operands.asArray(); }
  MutableArrayRef<Operand> getAllOperands() { return Operands.asArray(); }
  
  static bool classof(const ValueBase *V) {
    return V->getKind() == ValueKind::RefToBridgeObjectInst;
  }
};

/// Extract the heap object reference from a BridgeObject.
class BridgeObjectToRefInst
  : public UnaryInstructionBase<ValueKind::BridgeObjectToRefInst,
                                ConversionInst>
{
  friend class SILBuilder;

  BridgeObjectToRefInst(SILDebugLocation *DebugLoc, SILValue Operand,
                        SILType Ty)
      : UnaryInstructionBase(DebugLoc, Operand, Ty) {}
};

/// Retrieve the bit pattern of a BridgeObject.
class BridgeObjectToWordInst
  : public UnaryInstructionBase<ValueKind::BridgeObjectToWordInst,
                                ConversionInst>
{
  friend class SILBuilder;

  BridgeObjectToWordInst(SILDebugLocation *DebugLoc, SILValue Operand,
                         SILType Ty)
      : UnaryInstructionBase(DebugLoc, Operand, Ty) {}
};

/// RefToRawPointer - Convert a reference type to a Builtin.RawPointer.
class RefToRawPointerInst
  : public UnaryInstructionBase<ValueKind::RefToRawPointerInst, ConversionInst>
{
  friend class SILBuilder;

  RefToRawPointerInst(SILDebugLocation *DebugLoc, SILValue Operand, SILType Ty)
      : UnaryInstructionBase(DebugLoc, Operand, Ty) {}
};

/// RawPointerToRefInst - Convert a Builtin.RawPointer to a reference type.
class RawPointerToRefInst
  : public UnaryInstructionBase<ValueKind::RawPointerToRefInst, ConversionInst>
{
  friend class SILBuilder;

  RawPointerToRefInst(SILDebugLocation *DebugLoc, SILValue Operand, SILType Ty)
      : UnaryInstructionBase(DebugLoc, Operand, Ty) {}
};
  
/// RefToUnownedInst - Given a value of a reference type,
/// convert it to an unowned reference.
///
/// This does nothing at runtime; it just changes the formal type.
class RefToUnownedInst
  : public UnaryInstructionBase<ValueKind::RefToUnownedInst, ConversionInst>
{
  friend class SILBuilder;

  RefToUnownedInst(SILDebugLocation *DebugLoc, SILValue Operand, SILType Ty)
      : UnaryInstructionBase(DebugLoc, Operand, Ty) {}
};

/// UnownedToRefInst - Given a value of an @unowned type,
/// convert it to the underlying reference type.
///
/// This does nothing at runtime; it just changes the formal type.
class UnownedToRefInst
  : public UnaryInstructionBase<ValueKind::UnownedToRefInst, ConversionInst>
{
  friend class SILBuilder;

  UnownedToRefInst(SILDebugLocation *DebugLoc, SILValue Operand, SILType Ty)
      : UnaryInstructionBase(DebugLoc, Operand, Ty) {}
};

/// RefToUnmanagedInst - Given a value of a reference type,
/// convert it to an unmanaged reference.
///
/// This does nothing at runtime; it just changes the formal type.
class RefToUnmanagedInst
  : public UnaryInstructionBase<ValueKind::RefToUnmanagedInst, ConversionInst>
{
  friend class SILBuilder;

  RefToUnmanagedInst(SILDebugLocation *DebugLoc, SILValue Operand, SILType Ty)
      : UnaryInstructionBase(DebugLoc, Operand, Ty) {}
};

/// UnmanagedToRefInst - Given a value of an unmanaged reference type,
/// convert it to the underlying reference type.
///
/// This does nothing at runtime; it just changes the formal type.
class UnmanagedToRefInst
  : public UnaryInstructionBase<ValueKind::UnmanagedToRefInst, ConversionInst>
{
  friend class SILBuilder;

  UnmanagedToRefInst(SILDebugLocation *DebugLoc, SILValue Operand, SILType Ty)
      : UnaryInstructionBase(DebugLoc, Operand, Ty) {}
};

/// ThinToThickFunctionInst - Given a thin function reference, adds a null
/// context to convert the value to a thick function type.
class ThinToThickFunctionInst
  : public UnaryInstructionBase<ValueKind::ThinToThickFunctionInst,
                                ConversionInst>
{
  friend class SILBuilder;

  ThinToThickFunctionInst(SILDebugLocation *DebugLoc, SILValue Operand,
                          SILType Ty)
      : UnaryInstructionBase(DebugLoc, Operand, Ty) {}

public:
  /// Return the callee of the thin_to_thick_function.
  ///
  /// This is not technically necessary, but from a symmetry perspective it
  /// makes sense to follow the lead of partial_apply which also creates
  /// closures.
  SILValue getCallee() const { return getOperand(); }
};

/// Given a thick metatype value, produces an Objective-C metatype
/// value.
class ThickToObjCMetatypeInst
  : public UnaryInstructionBase<ValueKind::ThickToObjCMetatypeInst,
                                ConversionInst>
{
  friend class SILBuilder;

  ThickToObjCMetatypeInst(SILDebugLocation *DebugLoc, SILValue Operand,
                          SILType Ty)
      : UnaryInstructionBase(DebugLoc, Operand, Ty) {}
};

/// Given an Objective-C metatype value, produces a thick metatype
/// value.
class ObjCToThickMetatypeInst
  : public UnaryInstructionBase<ValueKind::ObjCToThickMetatypeInst,
                                ConversionInst>
{
  friend class SILBuilder;

  ObjCToThickMetatypeInst(SILDebugLocation *DebugLoc, SILValue Operand,
                          SILType Ty)
      : UnaryInstructionBase(DebugLoc, Operand, Ty) {}
};

/// Given an Objective-C metatype value, convert it to an AnyObject value.
class ObjCMetatypeToObjectInst
  : public UnaryInstructionBase<ValueKind::ObjCMetatypeToObjectInst,
                                ConversionInst>
{
  friend class SILBuilder;

  ObjCMetatypeToObjectInst(SILDebugLocation *DebugLoc, SILValue Operand,
                           SILType Ty)
      : UnaryInstructionBase(DebugLoc, Operand, Ty) {}
};

/// Given an Objective-C existential metatype value, convert it to an AnyObject
/// value.
class ObjCExistentialMetatypeToObjectInst
  : public UnaryInstructionBase<ValueKind::ObjCExistentialMetatypeToObjectInst,
                                ConversionInst>
{
  friend class SILBuilder;

  ObjCExistentialMetatypeToObjectInst(SILDebugLocation *DebugLoc,
                                      SILValue Operand, SILType Ty)
      : UnaryInstructionBase(DebugLoc, Operand, Ty) {}
};

/// Return the Objective-C Protocol class instance for a protocol.
class ObjCProtocolInst : public SILInstruction
{
  friend class SILBuilder;

  ProtocolDecl *Proto;
  ObjCProtocolInst(SILDebugLocation *DebugLoc, ProtocolDecl *Proto, SILType Ty)
      : SILInstruction(ValueKind::ObjCProtocolInst, DebugLoc, Ty),
        Proto(Proto) {}

public:
  ProtocolDecl *getProtocol() const { return Proto; }

  ArrayRef<Operand> getAllOperands() const { return {}; }
  MutableArrayRef<Operand> getAllOperands() { return {}; }

  /// getType() is ok since this is known to only have one type.
  SILType getType(unsigned i = 0) const { return ValueBase::getType(i); }

  static bool classof(const ValueBase *V) {
    return V->getKind() == ValueKind::ObjCProtocolInst;
  }
};

/// Test that an address or reference type is not null.
class IsNonnullInst : public UnaryInstructionBase<ValueKind::IsNonnullInst> {
  friend class SILBuilder;

  IsNonnullInst(SILDebugLocation *DebugLoc, SILValue Operand, SILType BoolTy)
      : UnaryInstructionBase(DebugLoc, Operand, BoolTy) {}
};
  

/// Perform an unconditional checked cast that aborts if the cast fails.
class UnconditionalCheckedCastInst
  : public UnaryInstructionBase<ValueKind::UnconditionalCheckedCastInst,
                                ConversionInst>
{
  friend class SILBuilder;

  UnconditionalCheckedCastInst(SILDebugLocation *DebugLoc, SILValue Operand,
                               SILType DestTy)
      : UnaryInstructionBase(DebugLoc, Operand, DestTy) {}
};

/// Perform an unconditional checked cast that aborts if the cast fails.
/// The result of the checked cast is left in the destination address.
class UnconditionalCheckedCastAddrInst : public SILInstruction
{
  friend class SILBuilder;

  enum {
    /// the value being stored
    Src,
    /// the lvalue being stored to
    Dest
  };
  FixedOperandList<2> Operands;
  CastConsumptionKind ConsumptionKind;
  CanType SourceType;
  CanType TargetType;

  UnconditionalCheckedCastAddrInst(SILDebugLocation *Loc,
                                   CastConsumptionKind consumption,
                                   SILValue src, CanType sourceType,
                                   SILValue dest, CanType targetType);

public:
  CastConsumptionKind getConsumptionKind() const { return ConsumptionKind; }

  SILValue getSrc() const { return Operands[Src].get(); }
  SILValue getDest() const { return Operands[Dest].get(); }

  /// Returns the formal type of the source value.
  CanType getSourceType() const { return SourceType; }

  /// Returns the formal target type.
  CanType getTargetType() const { return TargetType; }

  ArrayRef<Operand> getAllOperands() const { return Operands.asArray(); }
  MutableArrayRef<Operand> getAllOperands() { return Operands.asArray(); }

  static bool classof(const ValueBase *V) {
    return V->getKind() == ValueKind::UnconditionalCheckedCastAddrInst;
  }
};

/// StructInst - Represents a constructed loadable struct.
class StructInst : public SILInstruction {
  friend class SILBuilder;

  TailAllocatedOperandList<0> Operands;

  /// Because of the storage requirements of StructInst, object
  /// creation goes through 'create()'.
  StructInst(SILDebugLocation *DebugLoc, SILType Ty,
             ArrayRef<SILValue> Elements);

  /// Construct a StructInst.
  static StructInst *create(SILDebugLocation *DebugLoc, SILType Ty,
                            ArrayRef<SILValue> Elements, SILFunction &F);

public:
  /// The elements referenced by this StructInst.
  MutableArrayRef<Operand> getElementOperands() {
    return Operands.getDynamicAsArray();
  }

  /// The elements referenced by this StructInst.
  OperandValueArrayRef getElements() const {
    return Operands.getDynamicValuesAsArray();
  }

  /// getType() is ok since this is known to only have one type.
  SILType getType(unsigned i = 0) const { return ValueBase::getType(i); }

  ArrayRef<Operand> getAllOperands() const { return Operands.asArray(); }
  MutableArrayRef<Operand> getAllOperands() { return Operands.asArray(); }

  SILValue getFieldValue(const VarDecl *V) const {
    return getOperandForField(V)->get();
  }

  /// Return the Operand associated with the given VarDecl.
  const Operand *getOperandForField(const VarDecl *V) const {
    return const_cast<StructInst*>(this)->getOperandForField(V);
  }

  Operand *getOperandForField(const VarDecl *V) {
    // If V is null or is computed, there is no operand associated with it.
    assert(V && V->hasStorage() &&
           "getOperandForField only works with stored fields");

    StructDecl *S = getStructDecl();

    NominalTypeDecl::StoredPropertyRange Range = S->getStoredProperties();
    unsigned Index = 0;
    for (auto I = Range.begin(), E = Range.end(); I != E; ++I, ++Index)
      if (V == *I)
        return &getAllOperands()[Index];

    // Did not find a matching VarDecl, return nullptr.
    return nullptr;
  }

  /// Search the operands of this struct for a unique non-trivial field. If we
  /// find it, return it. Otherwise return SILValue().
  SILValue getUniqueNonTrivialFieldValue() {
    SILModule &Mod = getModule();
    ArrayRef<Operand> Ops = getAllOperands();

    Optional<unsigned> Index;
    // For each operand...
    for (unsigned i = 0, e = Ops.size(); i != e; ++i) {
      // If the operand is not trivial...
      if (!Ops[i].get().getType().isTrivial(Mod)) {
        // And we have not found an Index yet, set index to i and continue.
        if (!Index.hasValue()) {
          Index = i;
          continue;
        }

        // Otherwise, we have two values that are non-trivial. Bail.
        return SILValue();
      }
    }

    // If we did not find an index, return an empty SILValue.
    if (!Index.hasValue())
      return SILValue();

    // Otherwise, return the value associated with index.
    return Ops[Index.getValue()].get();
  }

  StructDecl *getStructDecl() const {
    auto s = getType().getStructOrBoundGenericStruct();
    assert(s && "A struct should always have a StructDecl associated with it");
    return s;
  }

  static bool classof(const ValueBase *V) {
    return V->getKind() == ValueKind::StructInst;
  }
};

/// RefCountingInst - An abstract class of instructions which
/// manipulate the reference count of their object operand.
class RefCountingInst : public SILInstruction {
protected:
  RefCountingInst(ValueKind Kind, SILDebugLocation *DebugLoc,
                  SILTypeList *TypeList = 0)
      : SILInstruction(Kind, DebugLoc, TypeList) {}

public:
  static bool classof(const ValueBase *V) {
    return V->getKind() >= ValueKind::First_RefCountingInst &&
           V->getKind() <= ValueKind::Last_RefCountingInst;
  }
};

/// RetainValueInst - Copies a loadable value.
class RetainValueInst : public UnaryInstructionBase<ValueKind::RetainValueInst,
                                                    RefCountingInst,
                                                    /*HasValue*/ false> {
  friend class SILBuilder;

  RetainValueInst(SILDebugLocation *DebugLoc, SILValue operand)
      : UnaryInstructionBase(DebugLoc, operand) {}
};

/// ReleaseValueInst - Destroys a loadable value.
class ReleaseValueInst : public UnaryInstructionBase<ValueKind::ReleaseValueInst,
                                                     RefCountingInst,
                                                     /*HasValue*/ false> {
  friend class SILBuilder;

  ReleaseValueInst(SILDebugLocation *DebugLoc, SILValue operand)
      : UnaryInstructionBase(DebugLoc, operand) {}
};

/// Transfers ownership of a loadable value to the current autorelease pool.
class AutoreleaseValueInst
                  : public UnaryInstructionBase<ValueKind::AutoreleaseValueInst,
                                                RefCountingInst,
                                                /*HasValue*/ false> {
  friend class SILBuilder;

  AutoreleaseValueInst(SILDebugLocation *DebugLoc, SILValue operand)
      : UnaryInstructionBase(DebugLoc, operand) {}
};

/// StrongPinInst - Ensure that the operand is retained and pinned, if
/// not by this operation then by some enclosing pin.
///
/// Transformations must not do anything which reorders pin and unpin
/// operations.  (This should generally be straightforward, as pin and
/// unpin may be conservatively assumed to have arbitrary
/// side-effects.)
class StrongPinInst
  : public UnaryInstructionBase<ValueKind::StrongPinInst, SILInstruction,
                                /*HasResult*/ true>
{
  friend class SILBuilder;

  StrongPinInst(SILDebugLocation *DebugLoc, SILValue operand);
};

/// StrongUnpinInst - Given that the operand is the result of a
/// strong_pin instruction, unpin it.
class StrongUnpinInst
  : public UnaryInstructionBase<ValueKind::StrongUnpinInst, SILInstruction,
                                /*HasResult*/ false>
{
  friend class SILBuilder;

  StrongUnpinInst(SILDebugLocation *DebugLoc, SILValue operand)
      : UnaryInstructionBase(DebugLoc, operand) {}
};

/// TupleInst - Represents a constructed loadable tuple.
class TupleInst : public SILInstruction {
  friend class SILBuilder;

  TailAllocatedOperandList<0> Operands;

  /// Because of the storage requirements of TupleInst, object
  /// creation goes through 'create()'.
  TupleInst(SILDebugLocation *DebugLoc, SILType Ty,
            ArrayRef<SILValue> Elements);

  /// Construct a TupleInst.
  static TupleInst *create(SILDebugLocation *DebugLoc, SILType Ty,
                           ArrayRef<SILValue> Elements, SILFunction &F);

public:
  /// The elements referenced by this TupleInst.
  MutableArrayRef<Operand> getElementOperands() {
    return Operands.getDynamicAsArray();
  }

  /// The elements referenced by this TupleInst.
  OperandValueArrayRef getElements() const {
    return Operands.getDynamicValuesAsArray();
  }

  // Return the i'th value referenced by this TupleInst.
  SILValue getElement(unsigned i) const {
    return getElements()[i];
  }

  /// getType() is ok since this is known to only have one type.
  SILType getType(unsigned i = 0) const { return ValueBase::getType(i); }

  ArrayRef<Operand> getAllOperands() const { return Operands.asArray(); }
  MutableArrayRef<Operand> getAllOperands() { return Operands.asArray(); }

  static bool classof(const ValueBase *V) {
    return V->getKind() == ValueKind::TupleInst;
  }

  TupleType *getTupleType() const {
    return getType().getSwiftRValueType()->castTo<TupleType>();
  }

  /// Search the operands of this tuple for a unique non-trivial elt. If we find
  /// it, return it. Otherwise return SILValue().
  SILValue getUniqueNonTrivialElt() {
    SILModule &Mod = getModule();
    ArrayRef<Operand> Ops = getAllOperands();

    Optional<unsigned> Index;
    // For each operand...
    for (unsigned i = 0, e = Ops.size(); i != e; ++i) {
      // If the operand is not trivial...
      if (!Ops[i].get().getType().isTrivial(Mod)) {
        // And we have not found an Index yet, set index to i and continue.
        if (!Index.hasValue()) {
          Index = i;
          continue;
        }

        // Otherwise, we have two values that are non-trivial. Bail.
        return SILValue();
      }
    }

    // If we did not find an index, return an empty SILValue.
    if (!Index.hasValue())
      return SILValue();

    // Otherwise, return the value associated with index.
    return Ops[Index.getValue()].get();
  }
};

/// Represents a loadable enum constructed from one of its
/// elements.
class EnumInst : public SILInstruction {
  friend class SILBuilder;

  Optional<FixedOperandList<1>> OptionalOperand;
  EnumElementDecl *Element;

  EnumInst(SILDebugLocation *DebugLoc, SILValue Operand,
           EnumElementDecl *Element, SILType ResultTy)
      : SILInstruction(ValueKind::EnumInst, DebugLoc, ResultTy),
        Element(Element) {
    if (Operand) {
      OptionalOperand.emplace(this, Operand);
    }
  }

public:
  EnumElementDecl *getElement() const { return Element; }

  bool hasOperand() const { return OptionalOperand.hasValue(); }
  SILValue getOperand() const { return OptionalOperand->asValueArray()[0]; }

  ArrayRef<Operand> getAllOperands() const {
    return OptionalOperand ? OptionalOperand->asArray() : ArrayRef<Operand>{};
  }

  MutableArrayRef<Operand> getAllOperands() {
    return OptionalOperand
      ? OptionalOperand->asArray() : MutableArrayRef<Operand>{};
  }

  SILType getType(unsigned i = 0) const { return ValueBase::getType(i); }

  static bool classof(const ValueBase *V) {
    return V->getKind() == ValueKind::EnumInst;
  }
};

/// Unsafely project the data for an enum case out of an enum without checking
/// the tag.
class UncheckedEnumDataInst
  : public UnaryInstructionBase<ValueKind::UncheckedEnumDataInst>
{
  friend class SILBuilder;

  EnumElementDecl *Element;

  UncheckedEnumDataInst(SILDebugLocation *DebugLoc, SILValue Operand,
                        EnumElementDecl *Element, SILType ResultTy)
      : UnaryInstructionBase(DebugLoc, Operand, ResultTy), Element(Element) {}

public:
  EnumElementDecl *getElement() const { return Element; }

  EnumDecl *getEnumDecl() const {
    auto *E = getOperand().getType().getEnumOrBoundGenericEnum();
    assert(E && "Operand of unchecked_enum_data must be of enum type");
    return E;
  }

  unsigned getElementNo() const {
    unsigned i = 0;
    for (EnumElementDecl *E : getEnumDecl()->getAllElements()) {
      if (E == Element)
        return i;
      ++i;
    }
    llvm_unreachable("An unchecked_enum_data's enumdecl should have at least "
                     "on element, the element that is being extracted");
  }
};

/// Projects the address of the data for a case inside an uninitialized enum in
/// order to initialize the payload for that case.
class InitEnumDataAddrInst
  : public UnaryInstructionBase<ValueKind::InitEnumDataAddrInst>
{
  friend class SILBuilder;

  EnumElementDecl *Element;

  InitEnumDataAddrInst(SILDebugLocation *DebugLoc, SILValue Operand,
                       EnumElementDecl *Element, SILType ResultTy)
      : UnaryInstructionBase(DebugLoc, Operand, ResultTy), Element(Element) {}

public:
  EnumElementDecl *getElement() const { return Element; }
};

/// InjectEnumAddrInst - Tags an enum as containing a case. The data for
/// that case, if any, must have been written into the enum first.
class InjectEnumAddrInst
  : public UnaryInstructionBase<ValueKind::InjectEnumAddrInst,
                                SILInstruction,
                                /*HAS_RESULT*/ false>
{
  friend class SILBuilder;

  EnumElementDecl *Element;

  InjectEnumAddrInst(SILDebugLocation *DebugLoc, SILValue Operand,
                     EnumElementDecl *Element)
      : UnaryInstructionBase(DebugLoc, Operand), Element(Element) {}

public:
  EnumElementDecl *getElement() const { return Element; }
};

/// Invalidate an enum value and take ownership of its payload data
/// without moving it in memory.
class UncheckedTakeEnumDataAddrInst
  : public UnaryInstructionBase<ValueKind::UncheckedTakeEnumDataAddrInst>
{
  friend class SILBuilder;

  EnumElementDecl *Element;

  UncheckedTakeEnumDataAddrInst(SILDebugLocation *DebugLoc, SILValue Operand,
                                EnumElementDecl *Element, SILType ResultTy)
      : UnaryInstructionBase(DebugLoc, Operand, ResultTy), Element(Element) {}

public:
  EnumElementDecl *getElement() const { return Element; }

  EnumDecl *getEnumDecl() const {
    auto *E = getOperand().getType().getEnumOrBoundGenericEnum();
    assert(E && "Operand of unchecked_take_enum_data_addr must be of enum"
                " type");
    return E;
  }

  unsigned getElementNo() const {
    unsigned i = 0;
    for (EnumElementDecl *E : getEnumDecl()->getAllElements()) {
      if (E == Element)
        return i;
      ++i;
    }
    llvm_unreachable(
        "An unchecked_enum_data_addr's enumdecl should have at least "
        "on element, the element that is being extracted");
  }
};

// Base class of all select instructions like select_enum, select_value, etc.
// The template parameter represents a type of case values to be compared
// with the operand of a select instruction.
template <class Derived, class T>
class SelectInstBase : public SILInstruction {
protected:
  unsigned NumCases : 31;
  unsigned HasDefault : 1;

  // The first operand is the operand of select_xxx instruction;
  // the rest are the case values and results of a select instruction.
  TailAllocatedOperandList<1> Operands;

public:
  SelectInstBase(ValueKind kind, SILDebugLocation *DebugLoc, SILType type,
                 unsigned numCases, bool hasDefault,
                 ArrayRef<SILValue> operands, SILValue operand)
      : SILInstruction(kind, DebugLoc, type), NumCases(numCases),
        HasDefault(hasDefault), Operands(this, operands, operand) {}

  SILValue getOperand() const { return Operands[0].get(); }

  ArrayRef<Operand> getAllOperands() const { return Operands.asArray(); }
  MutableArrayRef<Operand> getAllOperands() { return Operands.asArray(); }

  std::pair<T, SILValue> getCase(unsigned i) {
    return static_cast<const Derived *>(this)->getCase(i);
  }

  unsigned getNumCases() const { return NumCases; }

  bool hasDefault() const { return HasDefault; }

  SILValue getDefaultResult() const {
    return static_cast<const Derived *>(this)->getDefaultResult();
  }

  // getType() is OK because there's only one result.
  SILType getType() const { return SILInstruction::getType(0); }
};

/// Common base class for the select_enum and select_enum_addr instructions,
/// which select one of a set of possible results based on the case of an enum.
class SelectEnumInstBase
    : public SelectInstBase<SelectEnumInstBase, EnumElementDecl *> {
  // Tail-allocated after the operands is an array of `NumCases`
  // EnumElementDecl* pointers, referencing the case discriminators for each
  // operand.
  
  EnumElementDecl **getCaseBuf() {
    return reinterpret_cast<EnumElementDecl**>(Operands.asArray().end());
  }
  EnumElementDecl * const* getCaseBuf() const {
    return reinterpret_cast<EnumElementDecl* const*>(Operands.asArray().end());
  }

protected:
  SelectEnumInstBase(
      ValueKind Kind, SILDebugLocation *DebugLoc, SILValue Enum, SILType Type,
      SILValue DefaultValue,
      ArrayRef<std::pair<EnumElementDecl *, SILValue>> CaseValues);

  template <typename SELECT_ENUM_INST>
  static SELECT_ENUM_INST *
  createSelectEnum(SILDebugLocation *DebugLoc, SILValue Enum, SILType Type,
                   SILValue DefaultValue,
                   ArrayRef<std::pair<EnumElementDecl *, SILValue>> CaseValues,
                   SILFunction &F);

public:
  SILValue getEnumOperand() const { return getOperand(); }
  
  std::pair<EnumElementDecl*, SILValue>
  getCase(unsigned i) const {
    assert(i < NumCases && "case out of bounds");
    return std::make_pair(getCaseBuf()[i], Operands[i+1].get());
  }
  
  /// Return the value that will be used as the result for the specified enum
  /// case.
  SILValue getCaseResult(EnumElementDecl *D) {
    for (unsigned i = 0, e = getNumCases(); i != e; ++i) {
      auto Entry = getCase(i);
      if (Entry.first == D) return Entry.second;
    }
    // select_enum is required to be fully covered, so return the default if we
    // didn't find anything.
    return getDefaultResult();
  }
  
  /// \brief If the default refers to exactly one case decl, return it.
  NullablePtr<EnumElementDecl> getUniqueCaseForDefault();

  SILValue getDefaultResult() const {
    assert(HasDefault && "doesn't have a default");
    return Operands[NumCases + 1].get();
  }

  // If there is a single case that returns a literal "true" value (an
  // "integer_literal $Builtin.Int1, 1" value), return it.
  //
  // FIXME: This is used to interoperate with passes that reasoned about the
  // old enum_is_tag insn. Ideally those passes would become general enough
  // not to need this.
  NullablePtr<EnumElementDecl> getSingleTrueElement() const;
};
  
/// Select one of a set of values based on the case of an enum.
class SelectEnumInst : public SelectEnumInstBase {
  friend class SILBuilder;

private:
  friend class SelectEnumInstBase;

  SelectEnumInst(SILDebugLocation *DebugLoc, SILValue Operand, SILType Type,
                 SILValue DefaultValue,
                 ArrayRef<std::pair<EnumElementDecl *, SILValue>> CaseValues)
      : SelectEnumInstBase(ValueKind::SelectEnumInst, DebugLoc, Operand, Type,
                           DefaultValue, CaseValues) {}

  static SelectEnumInst *
  create(SILDebugLocation *DebugLoc, SILValue Operand, SILType Type,
         SILValue DefaultValue,
         ArrayRef<std::pair<EnumElementDecl *, SILValue>> CaseValues,
         SILFunction &F);

public:
  static bool classof(const ValueBase *V) {
    return V->getKind() == ValueKind::SelectEnumInst;
  }
};

/// Select one of a set of values based on the case of an enum.
class SelectEnumAddrInst : public SelectEnumInstBase {
  friend class SILBuilder;
  friend class SelectEnumInstBase;

  SelectEnumAddrInst(
      SILDebugLocation *DebugLoc, SILValue Operand, SILType Type,
      SILValue DefaultValue,
      ArrayRef<std::pair<EnumElementDecl *, SILValue>> CaseValues)
      : SelectEnumInstBase(ValueKind::SelectEnumAddrInst, DebugLoc, Operand,
                           Type, DefaultValue, CaseValues) {}

  static SelectEnumAddrInst *
  create(SILDebugLocation *DebugLoc, SILValue Operand, SILType Type,
         SILValue DefaultValue,
         ArrayRef<std::pair<EnumElementDecl *, SILValue>> CaseValues,
         SILFunction &F);

public:
  static bool classof(const ValueBase *V) {
    return V->getKind() == ValueKind::SelectEnumAddrInst;
  }
};

/// Select on a value of a builtin integer type.
class SelectValueInst : public SelectInstBase<SelectValueInst, SILValue> {
  friend class SILBuilder;

  SelectValueInst(SILDebugLocation *DebugLoc, SILValue Operand, SILType Type,
                  SILValue DefaultResult,
                  ArrayRef<SILValue> CaseValuesAndResults);

  static SelectValueInst *
  create(SILDebugLocation *DebugLoc, SILValue Operand, SILType Type,
         SILValue DefaultValue,
         ArrayRef<std::pair<SILValue, SILValue>> CaseValues, SILFunction &F);

  OperandValueArrayRef getCaseBuf() const {
    return Operands.getDynamicValuesAsArray();
  }

public:
  ~SelectValueInst();

  std::pair<SILValue, SILValue>
  getCase(unsigned i) const {
    assert(i < NumCases && "case out of bounds");
    return {getCaseBuf()[i*2], getCaseBuf()[i*2+1]};
  }

  SILValue getDefaultResult() const {
    assert(HasDefault && "doesn't have a default");
    return getCaseBuf()[NumCases*2];
  }

  static bool classof(const ValueBase *V) {
    return V->getKind() == ValueKind::SelectValueInst;
  }
};

/// MetatypeInst - Represents the production of an instance of a given metatype
/// named statically.
class MetatypeInst : public SILInstruction {
  friend class SILBuilder;

  /// Constructs a MetatypeInst
  MetatypeInst(SILDebugLocation *DebugLoc, SILType Metatype);

public:

  /// getType() is ok since this is known to only have one type.
  SILType getType(unsigned i = 0) const { return ValueBase::getType(i); }

  ArrayRef<Operand> getAllOperands() const { return {}; }
  MutableArrayRef<Operand> getAllOperands() { return {}; }

  static bool classof(const ValueBase *V) {
    return V->getKind() == ValueKind::MetatypeInst;
  }
};

/// Represents loading a dynamic metatype from a value.
class ValueMetatypeInst
  : public UnaryInstructionBase<ValueKind::ValueMetatypeInst>
{
  friend class SILBuilder;

  ValueMetatypeInst(SILDebugLocation *DebugLoc, SILType Metatype, SILValue Base)
      : UnaryInstructionBase(DebugLoc, Base, Metatype) {}
};

/// ExistentialMetatype - Represents loading a dynamic metatype from an
/// existential container.
class ExistentialMetatypeInst
  : public UnaryInstructionBase<ValueKind::ExistentialMetatypeInst>
{
  friend class SILBuilder;

  ExistentialMetatypeInst(SILDebugLocation *DebugLoc, SILType Metatype,
                          SILValue Base)
      : UnaryInstructionBase(DebugLoc, Base, Metatype) {}
};

/// Extract a numbered element out of a value of tuple type.
class TupleExtractInst
  : public UnaryInstructionBase<ValueKind::TupleExtractInst>
{
  friend class SILBuilder;

  unsigned FieldNo;

  TupleExtractInst(SILDebugLocation *DebugLoc, SILValue Operand,
                   unsigned FieldNo, SILType ResultTy)
      : UnaryInstructionBase(DebugLoc, Operand, ResultTy), FieldNo(FieldNo) {}

public:
  unsigned getFieldNo() const { return FieldNo; }

  TupleType *getTupleType() const {
    return getOperand().getType().getSwiftRValueType()->castTo<TupleType>();
  }

  unsigned getNumTupleElts() const {
    return getTupleType()->getNumElements();
  }

  /// Returns true if this is a trivial result of a tuple that is non-trivial
  /// and represents one RCID.
  bool isTrivialEltOfOneRCIDTuple() const;
  bool isEltOnlyNonTrivialElt() const;
};

/// Derive the address of a numbered element from the address of a tuple.
class TupleElementAddrInst
  : public UnaryInstructionBase<ValueKind::TupleElementAddrInst>
{
  friend class SILBuilder;

  unsigned FieldNo;

  TupleElementAddrInst(SILDebugLocation *DebugLoc, SILValue Operand,
                       unsigned FieldNo, SILType ResultTy)
      : UnaryInstructionBase(DebugLoc, Operand, ResultTy), FieldNo(FieldNo) {}

public:
  unsigned getFieldNo() const { return FieldNo; }


  TupleType *getTupleType() const {
    return getOperand().getType().getSwiftRValueType()->castTo<TupleType>();
  }
};

/// Extract a physical, fragile field out of a value of struct type.
class StructExtractInst
  : public UnaryInstructionBase<ValueKind::StructExtractInst>
{
  friend class SILBuilder;

  VarDecl *Field;

  StructExtractInst(SILDebugLocation *DebugLoc, SILValue Operand,
                    VarDecl *Field, SILType ResultTy)
      : UnaryInstructionBase(DebugLoc, Operand, ResultTy), Field(Field) {}

public:
  VarDecl *getField() const { return Field; }

  unsigned getFieldNo() const {
    unsigned i = 0;
    for (VarDecl *D : getStructDecl()->getStoredProperties()) {
      if (Field == D)
        return i;
      ++i;
    }
    llvm_unreachable("A struct_extract's structdecl has at least 1 field, the "
                     "field of the struct_extract.");
  }

  StructDecl *getStructDecl() const {
    auto s = getOperand().getType().getStructOrBoundGenericStruct();
    assert(s);
    return s;
  }

  /// Returns true if this is a trivial result of a struct that is non-trivial
  /// and represents one RCID.
  bool isTrivialFieldOfOneRCIDStruct() const;

  /// Return true if we are extracting the only non-trivial field of out parent
  /// struct. This implies that a ref count operation on the aggregate is
  /// equivalent to a ref count operation on this field.
  bool isFieldOnlyNonTrivialField() const;
};

/// Derive the address of a physical field from the address of a struct.
class StructElementAddrInst
  : public UnaryInstructionBase<ValueKind::StructElementAddrInst>
{
  friend class SILBuilder;

  VarDecl *Field;

  StructElementAddrInst(SILDebugLocation *DebugLoc, SILValue Operand,
                        VarDecl *Field, SILType ResultTy)
      : UnaryInstructionBase(DebugLoc, Operand, ResultTy), Field(Field) {}

public:
  VarDecl *getField() const { return Field; }

  unsigned getFieldNo() const {
    unsigned i = 0;
    for (auto *D : getStructDecl()->getStoredProperties()) {
      if (Field == D)
        return i;
      ++i;
    }
    llvm_unreachable("A struct_element_addr's structdecl has at least 1 field, "
                     "the field of the struct_element_addr.");
  }

  StructDecl *getStructDecl() const {
    auto s = getOperand().getType().getStructOrBoundGenericStruct();
    assert(s);
    return s;
  }
};

/// RefElementAddrInst - Derive the address of a named element in a reference
/// type instance.
class RefElementAddrInst
  : public UnaryInstructionBase<ValueKind::RefElementAddrInst>
{
  friend class SILBuilder;

  VarDecl *Field;

  RefElementAddrInst(SILDebugLocation *DebugLoc, SILValue Operand,
                     VarDecl *Field, SILType ResultTy)
      : UnaryInstructionBase(DebugLoc, Operand, ResultTy), Field(Field) {}

public:
  VarDecl *getField() const { return Field; }

  unsigned getFieldNo() const {
    unsigned i = 0;
    for (auto *D : getClassDecl()->getStoredProperties()) {
      if (Field == D)
        return i;
      ++i;
    }
    llvm_unreachable("A ref_element_addr's classdecl has at least 1 field, the "
                     "field of the ref_element_addr.");
  }

  ClassDecl *getClassDecl() const {
    auto s = getOperand().getType().getClassOrBoundGenericClass();
    assert(s);
    return s;
  }
};

/// MethodInst - Abstract base for instructions that implement dynamic
/// method lookup.
class MethodInst : public SILInstruction {
  SILDeclRef Member;
  bool Volatile;
public:
  MethodInst(ValueKind Kind, SILDebugLocation *DebugLoc, SILType Ty,
             SILDeclRef Member, bool Volatile = false)
      : SILInstruction(Kind, DebugLoc, Ty), Member(Member), Volatile(Volatile) {
  }

  /// getType() is ok since this is known to only have one type.
  SILType getType(unsigned i = 0) const { return ValueBase::getType(i); }

  SILDeclRef getMember() const { return Member; }

  /// True if this dynamic dispatch is semantically required.
  bool isVolatile() const { return Volatile; }

  static bool classof(const ValueBase *V) {
    return V->getKind() >= ValueKind::First_MethodInst &&
      V->getKind() <= ValueKind::Last_MethodInst;
  }
};

/// ClassMethodInst - Given the address of a value of class type and a method
/// constant, extracts the implementation of that method for the dynamic
/// instance type of the class.
class ClassMethodInst
  : public UnaryInstructionBase<ValueKind::ClassMethodInst, MethodInst>
{
  friend class SILBuilder;

  ClassMethodInst(SILDebugLocation *DebugLoc, SILValue Operand,
                  SILDeclRef Member, SILType Ty, bool Volatile = false)
      : UnaryInstructionBase(DebugLoc, Operand, Ty, Member, Volatile) {}
};

/// SuperMethodInst - Given the address of a value of class type and a method
/// constant, extracts the implementation of that method for the superclass of
/// the static type of the class.
class SuperMethodInst
  : public UnaryInstructionBase<ValueKind::SuperMethodInst, MethodInst>
{
  friend class SILBuilder;

  SuperMethodInst(SILDebugLocation *DebugLoc, SILValue Operand,
                  SILDeclRef Member, SILType Ty, bool Volatile = false)
      : UnaryInstructionBase(DebugLoc, Operand, Ty, Member, Volatile) {}
};

/// WitnessMethodInst - Given a type, a protocol conformance,
/// and a protocol method constant, extracts the implementation of that method
/// for the type.
/// If this witness_method is on an opened existential type it needs the opened
/// value as operand.
class WitnessMethodInst : public MethodInst {
  friend class SILBuilder;

  CanType LookupType;
  ProtocolConformance *Conformance;
  Optional<FixedOperandList<1>> OptionalOperand;

  WitnessMethodInst(SILDebugLocation *DebugLoc, CanType LookupType,
                    ProtocolConformance *Conformance, SILDeclRef Member,
                    SILType Ty, SILValue OpenedExistential,
                    bool Volatile = false)
      : MethodInst(ValueKind::WitnessMethodInst, DebugLoc, Ty, Member,
                   Volatile),
        LookupType(LookupType), Conformance(Conformance) {
    if (OpenedExistential)
      OptionalOperand.emplace(this, OpenedExistential);
  }

  static WitnessMethodInst *
  create(SILDebugLocation *DebugLoc, CanType LookupType,
         ProtocolConformance *Conformance, SILDeclRef Member, SILType Ty,
         SILFunction *Parent, SILValue OpenedExistential,
         bool Volatile = false);

public:

  CanType getLookupType() const { return LookupType; }
  ProtocolDecl *getLookupProtocol() const {
    return getMember().getDecl()->getDeclContext()
             ->isProtocolOrProtocolExtensionContext();
  }
  ProtocolConformance *getConformance() const { return Conformance; }

  /// Get a representation of the lookup type as a substitution of the
  /// protocol's Self archetype.
  Substitution getSelfSubstitution() const {
    auto memberDC = getMember().getDecl()->getDeclContext();
    return Substitution{memberDC->getProtocolSelf()->getArchetype(),
                        getLookupType(),
                        Conformance};
  }

  bool hasOperand() const { return OptionalOperand.hasValue(); }
  SILValue getOperand() const {
    assert(hasOperand() && "Missing operand");
    return OptionalOperand->asValueArray()[0];
  }

  ArrayRef<Operand> getAllOperands() const {
    return OptionalOperand ? OptionalOperand->asArray() : ArrayRef<Operand>{};
  }

  MutableArrayRef<Operand> getAllOperands() {
    return OptionalOperand ? OptionalOperand->asArray()
                           : MutableArrayRef<Operand>{};
  }

  static bool classof(const ValueBase *V) {
    return V->getKind() == ValueKind::WitnessMethodInst;
  }
};

/// Given the address of a value of AnyObject protocol type and a method
/// constant referring to some Objective-C method, performs dynamic method
/// lookup to extract the implementation of that method. This method lookup
/// can fail at run-time
class DynamicMethodInst
  : public UnaryInstructionBase<ValueKind::DynamicMethodInst, MethodInst>
{
  friend class SILBuilder;

  DynamicMethodInst(SILDebugLocation *DebugLoc, SILValue Operand,
                    SILDeclRef Member, SILType Ty, bool Volatile = false)
      : UnaryInstructionBase(DebugLoc, Operand, Ty, Member, Volatile) {}
};

/// Given the address of an existential, "opens" the
/// existential by returning a pointer to a fresh archetype T, which also
/// captures the (dynamic) conformances.
class OpenExistentialAddrInst
  : public UnaryInstructionBase<ValueKind::OpenExistentialAddrInst>
{
  friend class SILBuilder;

  OpenExistentialAddrInst(SILDebugLocation *DebugLoc, SILValue Operand,
                          SILType SelfTy)
      : UnaryInstructionBase(DebugLoc, Operand, SelfTy) {}
};

/// Given a class existential, "opens" the
/// existential by returning a pointer to a fresh archetype T, which also
/// captures the (dynamic) conformances.
class OpenExistentialRefInst
  : public UnaryInstructionBase<ValueKind::OpenExistentialRefInst>
{
  friend class SILBuilder;

  OpenExistentialRefInst(SILDebugLocation *DebugLoc, SILValue Operand,
                         SILType Ty)
      : UnaryInstructionBase(DebugLoc, Operand, Ty) {}
};

/// Given an existential metatype,
/// "opens" the existential by returning a pointer to a fresh
/// archetype metatype T.Type, which also captures the (dynamic)
/// conformances.
class OpenExistentialMetatypeInst
  : public UnaryInstructionBase<ValueKind::OpenExistentialMetatypeInst>
{
  friend class SILBuilder;

  OpenExistentialMetatypeInst(SILDebugLocation *DebugLoc, SILValue operand,
                              SILType ty)
      : UnaryInstructionBase(DebugLoc, operand, ty) {}
};

/// Given a boxed existential container,
/// "opens" the existential by returning a pointer to a fresh
/// archetype T, which also captures the (dynamic) conformances.
class OpenExistentialBoxInst
  : public UnaryInstructionBase<ValueKind::OpenExistentialBoxInst>
{
  friend class SILBuilder;

  OpenExistentialBoxInst(SILDebugLocation *DebugLoc, SILValue operand,
                         SILType ty)
      : UnaryInstructionBase(DebugLoc, operand, ty) {}
};

/// Given an address to an uninitialized buffer of
/// a protocol type, initializes its existential container to contain a concrete
/// value of the given type, and returns the address of the uninitialized
/// concrete value inside the existential container.
class InitExistentialAddrInst
  : public UnaryInstructionBase<ValueKind::InitExistentialAddrInst>
{
  friend class SILBuilder;

  CanType ConcreteType;
  ArrayRef<ProtocolConformance*> Conformances;

  InitExistentialAddrInst(SILDebugLocation *DebugLoc, SILValue Existential,
                          CanType ConcreteType, SILType ConcreteLoweredType,
                          ArrayRef<ProtocolConformance *> Conformances)
      : UnaryInstructionBase(DebugLoc, Existential,
                             ConcreteLoweredType.getAddressType()),
        ConcreteType(ConcreteType), Conformances(Conformances) {}

  static InitExistentialAddrInst *
  create(SILDebugLocation *DebugLoc, SILValue Existential, CanType ConcreteType,
         SILType ConcreteLoweredType,
         ArrayRef<ProtocolConformance *> Conformances, SILFunction *Parent);

public:
  ArrayRef<ProtocolConformance*> getConformances() const {
    return Conformances;
  }
  
  CanType getFormalConcreteType() const {
    return ConcreteType;
  }

  SILType getLoweredConcreteType() const {
    return getType();
  }
};

/// InitExistentialRefInst - Given a class instance reference and a set of
/// conformances, creates a class existential value referencing the
/// class instance.
class InitExistentialRefInst
  : public UnaryInstructionBase<ValueKind::InitExistentialRefInst>
{
  friend class SILBuilder;

  CanType ConcreteType;
  ArrayRef<ProtocolConformance*> Conformances;

  InitExistentialRefInst(SILDebugLocation *DebugLoc, SILType ExistentialType,
                         CanType FormalConcreteType, SILValue Instance,
                         ArrayRef<ProtocolConformance *> Conformances)
      : UnaryInstructionBase(DebugLoc, Instance, ExistentialType),
        ConcreteType(FormalConcreteType), Conformances(Conformances) {}

  static InitExistentialRefInst *
  create(SILDebugLocation *DebugLoc, SILType ExistentialType,
         CanType ConcreteType, SILValue Instance,
         ArrayRef<ProtocolConformance *> Conformances, SILFunction *Parent);

public:
  CanType getFormalConcreteType() const {
    return ConcreteType;
  }

  ArrayRef<ProtocolConformance*> getConformances() const {
    return Conformances;
  }
};

/// InitExistentialMetatypeInst - Given a metatype reference and a set
/// of conformances, creates an existential metatype value referencing
/// the metatype.
class InitExistentialMetatypeInst
  : public UnaryInstructionBase<ValueKind::InitExistentialMetatypeInst>
{
  friend class SILBuilder;

  /// Pointer to the last of our tail allocated conformances. Null if this
  /// existential metatype does not have any conformances.
  NullablePtr<ProtocolConformance *> LastConformance;

  InitExistentialMetatypeInst(SILDebugLocation *DebugLoc,
                              SILType existentialMetatypeType,
                              SILValue metatype,
                              ArrayRef<ProtocolConformance *> conformances);

  static InitExistentialMetatypeInst *
  create(SILDebugLocation *DebugLoc, SILType existentialMetatypeType,
         SILValue metatype, ArrayRef<ProtocolConformance *> conformances,
         SILFunction *parent);

public:
  /// Return the object type which was erased.  That is, if this
  /// instruction erases Decoder<T>.Type.Type to Printable.Type.Type,
  /// this method returns Decoder<T>.
  CanType getFormalErasedObjectType() const {
    CanType exType = getType().getSwiftRValueType();
    CanType concreteType = getOperand().getType().getSwiftRValueType();
    while (auto exMetatype = dyn_cast<ExistentialMetatypeType>(exType)) {
      exType = exMetatype.getInstanceType();
      concreteType = cast<MetatypeType>(concreteType).getInstanceType();
    }
    assert(exType.isExistentialType());
    return concreteType;
  }

  ArrayRef<ProtocolConformance *> getConformances() const;
};

/// DeinitExistentialAddrInst - Given an address of an existential that has been
/// partially initialized with an InitExistentialAddrInst but whose value buffer
/// has not been initialized, deinitializes the existential and deallocates
/// the value buffer. This should only be used for partially-initialized
/// existentials; a fully-initialized existential can be destroyed with
/// DestroyAddrInst and deallocated with DeallocStackInst.
class DeinitExistentialAddrInst
  : public UnaryInstructionBase<ValueKind::DeinitExistentialAddrInst,
                                SILInstruction,
                                /*HAS_RESULT*/ false>
{
  friend class SILBuilder;

  DeinitExistentialAddrInst(SILDebugLocation *DebugLoc, SILValue Existential)
      : UnaryInstructionBase(DebugLoc, Existential) {}
};

/// Projects the capture storage address from a @block_storage address.
class ProjectBlockStorageInst
  : public UnaryInstructionBase<ValueKind::ProjectBlockStorageInst,
                                SILInstruction>
{
  friend class SILBuilder;

  ProjectBlockStorageInst(SILDebugLocation *DebugLoc, SILValue Operand,
                          SILType DestTy)
      : UnaryInstructionBase(DebugLoc, Operand, DestTy) {}
};

///

/// Initializes a block header, creating a block that
/// invokes a given thin cdecl function.
class InitBlockStorageHeaderInst : public SILInstruction {
  friend class SILBuilder;

  enum { BlockStorage, InvokeFunction };
  FixedOperandList<2> Operands;

  InitBlockStorageHeaderInst(SILDebugLocation *DebugLoc, SILValue BlockStorage,
                             SILValue InvokeFunction, SILType BlockType)
      : SILInstruction(ValueKind::InitBlockStorageHeaderInst, DebugLoc,
                       BlockType),
        Operands(this, BlockStorage, InvokeFunction) {}

public:
  /// Get the block storage address to be initialized.
  SILValue getBlockStorage() const { return Operands[BlockStorage].get(); }
  /// Get the invoke function to form the block around.
  SILValue getInvokeFunction() const { return Operands[InvokeFunction].get(); }

  ArrayRef<Operand> getAllOperands() const { return Operands.asArray(); }
  MutableArrayRef<Operand> getAllOperands() { return Operands.asArray(); }

  /// getType() is OK since there's only one result.
  SILType getType() const { return SILInstruction::getType(0); }

  static bool classof(const ValueBase *V) {
    return V->getKind() == ValueKind::InitBlockStorageHeaderInst;
  }
};

/// StrongRetainInst - Increase the strong reference count of an object.
class StrongRetainInst
  : public UnaryInstructionBase<ValueKind::StrongRetainInst,
                                RefCountingInst,
                                /*HAS_RESULT*/ false>
{
  friend class SILBuilder;

  StrongRetainInst(SILDebugLocation *DebugLoc, SILValue Operand)
      : UnaryInstructionBase(DebugLoc, Operand) {}
};

/// StrongRetainAutoreleasedInst - Take ownership of the autoreleased return
/// value of an ObjC method.
class StrongRetainAutoreleasedInst
  : public UnaryInstructionBase<ValueKind::StrongRetainAutoreleasedInst,
                                RefCountingInst, /*HAS_RESULT*/ false>
{
  friend class SILBuilder;

  StrongRetainAutoreleasedInst(SILDebugLocation *DebugLoc, SILValue Operand)
      : UnaryInstructionBase(DebugLoc, Operand) {}
};

/// StrongReleaseInst - Decrease the strong reference count of an object.
///
/// An object can be destroyed when its strong reference count is
/// zero.  It can be deallocated when both its strong reference and
/// weak reference counts reach zero.
class StrongReleaseInst
  : public UnaryInstructionBase<ValueKind::StrongReleaseInst,
                                RefCountingInst, /*HAS_RESULT*/ false>
{
  friend class SILBuilder;

  StrongReleaseInst(SILDebugLocation *DebugLoc, SILValue Operand)
      : UnaryInstructionBase(DebugLoc, Operand) {}
};

/// StrongRetainUnownedInst - Increase the strong reference count of an object
/// and assert that it has not been deallocated.
///
/// The operand must be an @unowned type.
class StrongRetainUnownedInst :
    public UnaryInstructionBase<ValueKind::StrongRetainUnownedInst,
                                RefCountingInst, /*HAS_RESULT*/ false>
{
  friend class SILBuilder;

  StrongRetainUnownedInst(SILDebugLocation *DebugLoc, SILValue operand)
      : UnaryInstructionBase(DebugLoc, operand) {}
};

/// UnownedRetainInst - Increase the unowned reference count of an object.
class UnownedRetainInst :
    public UnaryInstructionBase<ValueKind::UnownedRetainInst,
                                RefCountingInst, /*HAS_RESULT*/ false>
{
  friend class SILBuilder;

  UnownedRetainInst(SILDebugLocation *DebugLoc, SILValue Operand)
      : UnaryInstructionBase(DebugLoc, Operand) {}
};

/// UnownedReleaseInst - Decrease the unowned reference count of an object.
class UnownedReleaseInst :
     public UnaryInstructionBase<ValueKind::UnownedReleaseInst,
                                 RefCountingInst, /*HAS_RESULT*/ false>
{
  friend class SILBuilder;

  UnownedReleaseInst(SILDebugLocation *DebugLoc, SILValue Operand)
      : UnaryInstructionBase(DebugLoc, Operand) {}
};

/// FixLifetimeInst - An artificial use of a value for the purposes of ARC or
/// RVO optimizations.
class FixLifetimeInst :
  public UnaryInstructionBase<ValueKind::FixLifetimeInst,
                              SILInstruction, /*HAS_RESULT*/ false>
{
  friend class SILBuilder;

  FixLifetimeInst(SILDebugLocation *DebugLoc, SILValue Operand)
      : UnaryInstructionBase(DebugLoc, Operand) {}
};

/// MarkDependenceInst - Marks that one value depends on another for
/// validity in a non-obvious way.
class MarkDependenceInst : public SILInstruction {
  friend class SILBuilder;

  enum { Value, Base };
  FixedOperandList<2> Operands;

  MarkDependenceInst(SILDebugLocation *DebugLoc, SILValue value, SILValue base)
      : SILInstruction(ValueKind::MarkDependenceInst, DebugLoc,
                       value.getType()),
        Operands{this, value, base} {}

public:
  SILValue getValue() const { return Operands[Value].get(); }
  SILValue getBase() const { return Operands[Base].get(); }

  ArrayRef<Operand> getAllOperands() const { return Operands.asArray(); }
  MutableArrayRef<Operand> getAllOperands() { return Operands.asArray(); }

  SILType getType(unsigned i = 0) const { return ValueBase::getType(i); }

  static bool classof(const ValueBase *V) {
    return V->getKind() == ValueKind::MarkDependenceInst;
  }
};

/// Promote an Objective-C block that is on the stack to the heap, or simply
/// retain a block that is already on the heap.
class CopyBlockInst :
    public UnaryInstructionBase<ValueKind::CopyBlockInst,
                                SILInstruction, /*HAS_RESULT*/ true>
{
  friend class SILBuilder;

  CopyBlockInst(SILDebugLocation *DebugLoc, SILValue operand)
      : UnaryInstructionBase(DebugLoc, operand, operand.getType()) {}
};

/// Given an object reference, return true iff it is non-nil and refers
/// to a native swift object with strong reference count of 1.
class IsUniqueInst : public UnaryInstructionBase<ValueKind::IsUniqueInst>
{
  friend class SILBuilder;

  IsUniqueInst(SILDebugLocation *DebugLoc, SILValue Operand, SILType BoolTy)
      : UnaryInstructionBase(DebugLoc, Operand, BoolTy) {}
};

/// Given an object reference, return true iff it is non-nil and either refers
/// to a native swift object with strong reference count of 1 or refers to a
/// pinned object (for simultaneous access to multiple subobjects).
class IsUniqueOrPinnedInst :
    public UnaryInstructionBase<ValueKind::IsUniqueOrPinnedInst> {
  friend class SILBuilder;

  IsUniqueOrPinnedInst(SILDebugLocation *DebugLoc, SILValue Operand,
                       SILType BoolTy)
      : UnaryInstructionBase(DebugLoc, Operand, BoolTy) {}
};

//===----------------------------------------------------------------------===//
// DeallocationInsts
//===----------------------------------------------------------------------===//

/// DeallocationInst - An abstract parent class for Dealloc{Stack, Box, Ref}.
class DeallocationInst : public SILInstruction {
protected:
  DeallocationInst(ValueKind Kind, SILDebugLocation *DebugLoc,
                   SILTypeList *TypeList = nullptr)
      : SILInstruction(Kind, DebugLoc, TypeList) {}

public:
  static bool classof(const ValueBase *V) {
    return V->getKind() >= ValueKind::First_DeallocationInst &&
      V->getKind() <= ValueKind::Last_DeallocationInst;
  }
};

/// DeallocStackInst - Deallocate stack memory allocated by alloc_stack.
class DeallocStackInst :
    public UnaryInstructionBase<ValueKind::DeallocStackInst, DeallocationInst,
                                /*HAS_RESULT*/ false> {
  friend class SILBuilder;

  DeallocStackInst(SILDebugLocation *DebugLoc, SILValue operand)
      : UnaryInstructionBase(DebugLoc, operand) {}
};

/// Deallocate memory for a reference type instance from a destructor or
/// failure path of a constructor.
///
/// This does not destroy the referenced instance; it must be destroyed
/// first.
///
/// It is undefined behavior if the type of the operand does not match the
/// most derived type of the allocated instance.
class DeallocRefInst :
  public UnaryInstructionBase<ValueKind::DeallocRefInst, DeallocationInst,
                              /*HAS_RESULT*/ false>,
  public StackPromotable {
  friend class SILBuilder;

private:
  DeallocRefInst(SILDebugLocation *DebugLoc, SILValue Operand,
                 bool canBeOnStack = false)
      : UnaryInstructionBase(DebugLoc, Operand), StackPromotable(canBeOnStack) {
  }
};

/// Deallocate memory for a reference type instance from a failure path of a
/// constructor.
///
/// The instance is assumed to have been partially initialized, with the
/// initialized portion being all instance variables in classes that are more
/// derived than the given metatype.
///
/// The metatype value can either be the static self type (in a designated
/// initializer) or a dynamic self type (in a convenience initializer).
class DeallocPartialRefInst : public DeallocationInst {
  friend class SILBuilder;

private:
  FixedOperandList<2> Operands;

  DeallocPartialRefInst(SILDebugLocation *DebugLoc, SILValue Operand,
                        SILValue Metatype)
      : DeallocationInst(ValueKind::DeallocPartialRefInst, DebugLoc),
        Operands(this, Operand, Metatype) {}

public:
  ArrayRef<Operand> getAllOperands() const { return Operands.asArray(); }
  MutableArrayRef<Operand> getAllOperands() { return Operands.asArray(); }
  
  SILValue getInstance() const { return getOperand(0); }
  SILValue getMetatype() const { return getOperand(1); }

  static bool classof(const ValueBase *V) {
    return V->getKind() == ValueKind::DeallocPartialRefInst;
  }
};

/// Deallocate memory allocated for a unsafe value buffer.
class DeallocValueBufferInst :
  public UnaryInstructionBase<ValueKind::DeallocValueBufferInst,
                              DeallocationInst, /*HAS_RESULT*/ true> {
  friend class SILBuilder;

  SILType ValueType;

  DeallocValueBufferInst(SILDebugLocation *DebugLoc, SILType valueType,
                         SILValue operand)
      : UnaryInstructionBase(DebugLoc, operand), ValueType(valueType) {}

public:
  SILType getValueType() const { return ValueType; }
};

/// Deallocate memory allocated for a boxed value created by an AllocBoxInst.
/// It is undefined behavior if the type of the boxed type does not match the
/// type the box was allocated for.
///
/// This does not destroy the boxed value instance; it must either be
/// uninitialized or have been manually destroyed.
class DeallocBoxInst :
  public UnaryInstructionBase<ValueKind::DeallocBoxInst, DeallocationInst,
                              /*HAS_RESULT*/ false>
{
  friend class SILBuilder;

  // TODO: The element type can be derived from a typed box.
  SILType ElementType;

  DeallocBoxInst(SILDebugLocation *DebugLoc, SILType elementType,
                 SILValue operand)
      : UnaryInstructionBase(DebugLoc, operand), ElementType(elementType) {}

public:
  SILType getElementType() const { return ElementType; }
};

/// Deallocate memory allocated for a boxed existential container created by
/// AllocExistentialBox. It is undefined behavior if the given concrete type
/// does not match the concrete type for which the box was allocated.
///
/// This does not destroy the boxed value instance; it must either be
/// uninitialized or have been manually destroyed.
class DeallocExistentialBoxInst :
  public UnaryInstructionBase<ValueKind::DeallocExistentialBoxInst,
                              DeallocationInst,
                              /*HAS_RESULT*/ false>
{
  friend class SILBuilder;

  CanType ConcreteType;

  DeallocExistentialBoxInst(SILDebugLocation *DebugLoc, CanType concreteType,
                            SILValue operand)
      : UnaryInstructionBase(DebugLoc, operand), ConcreteType(concreteType) {}

public:
  CanType getConcreteType() const { return ConcreteType; }
};

/// Destroy the value at a memory location according to
/// its SIL type. This is similar to:
///   %1 = load %operand
///   release_value %1
/// but a destroy instruction can be used for types that cannot be loaded,
/// such as resilient value types.
class DestroyAddrInst : public UnaryInstructionBase<ValueKind::DestroyAddrInst,
                                                    SILInstruction,
                                                    /*HAS_RESULT*/ false>
{
  friend class SILBuilder;

  DestroyAddrInst(SILDebugLocation *DebugLoc, SILValue Operand)
      : UnaryInstructionBase(DebugLoc, Operand) {}
};

/// Project out the address of the value
/// stored in the given Builtin.UnsafeValueBuffer.
class ProjectValueBufferInst :
  public UnaryInstructionBase<ValueKind::ProjectValueBufferInst,
                              SILInstruction, /*HasResult*/ true> {
  friend class SILBuilder;

  ProjectValueBufferInst(SILDebugLocation *DebugLoc, SILType valueType,
                         SILValue operand)
      : UnaryInstructionBase(DebugLoc, operand, valueType.getAddressType()) {}

public:
  SILType getValueType() const { return getType().getObjectType(); }
};

/// Project out the address of the value in a box.
class ProjectBoxInst :
  public UnaryInstructionBase<ValueKind::ProjectBoxInst,
                              SILInstruction, /*HasResult*/ true> {
  friend class SILBuilder;

  ProjectBoxInst(SILDebugLocation *DebugLoc, SILType valueType,
                 SILValue operand)
      : UnaryInstructionBase(DebugLoc, operand, valueType.getAddressType()) {}

public:
  SILType getValueType() const { return getType().getObjectType(); }
};

//===----------------------------------------------------------------------===//
// Runtime failure
//===----------------------------------------------------------------------===//

/// Trigger a runtime failure if the given Int1 value is true.
class CondFailInst : public UnaryInstructionBase<ValueKind::CondFailInst,
                                                 SILInstruction,
                                                 /*HAS_RESULT*/ false>
{
  friend class SILBuilder;

  CondFailInst(SILDebugLocation *DebugLoc, SILValue Operand)
      : UnaryInstructionBase(DebugLoc, Operand) {}
};

//===----------------------------------------------------------------------===//
// Pointer/address indexing instructions
//===----------------------------------------------------------------------===//

/// Abstract base class for indexing instructions.
class IndexingInst : public SILInstruction {
  enum { Base, Index };
  FixedOperandList<2> Operands;
public:
  IndexingInst(ValueKind Kind, SILDebugLocation *DebugLoc, SILValue Operand,
               SILValue Index)
      : SILInstruction(Kind, DebugLoc, Operand.getType()),
        Operands{this, Operand, Index} {}

  SILValue getBase() const { return Operands[Base].get(); }
  SILValue getIndex() const { return Operands[Index].get(); }

  ArrayRef<Operand> getAllOperands() const { return Operands.asArray(); }
  MutableArrayRef<Operand> getAllOperands() { return Operands.asArray(); }

  SILType getType(unsigned i = 0) const { return ValueBase::getType(i); }

  static bool classof(const ValueBase *V) {
    return V->getKind() >= ValueKind::First_IndexingInst
        && V->getKind() <= ValueKind::Last_IndexingInst;
  }
};

/// IndexAddrInst - "%2 : $*T = index_addr %0 : $*T, %1 : $Builtin.Int64"
/// This takes an address and indexes it, striding by the pointed-
/// to type.  This is used to index into arrays of uniform elements.
class IndexAddrInst : public IndexingInst {
  friend class SILBuilder;

  enum { Base, Index };

  IndexAddrInst(SILDebugLocation *DebugLoc, SILValue Operand, SILValue Index)
      : IndexingInst(ValueKind::IndexAddrInst, DebugLoc, Operand, Index) {}

public:
  static bool classof(const ValueBase *V) {
    return V->getKind() == ValueKind::IndexAddrInst;
  }
};

/// IndexRawPointerInst
/// %2 : $Builtin.RawPointer \
///   = index_raw_pointer %0 : $Builtin.RawPointer, %1 : $Builtin.Int64
/// This takes an address and indexes it, striding by the pointed-
/// to type.  This is used to index into arrays of uniform elements.
class IndexRawPointerInst : public IndexingInst {
  friend class SILBuilder;

  enum { Base, Index };

  IndexRawPointerInst(SILDebugLocation *DebugLoc, SILValue Operand,
                      SILValue Index)
      : IndexingInst(ValueKind::IndexRawPointerInst, DebugLoc, Operand, Index) {
  }

public:
  static bool classof(const ValueBase *V) {
    return V->getKind() == ValueKind::IndexRawPointerInst;
  }
};

//===----------------------------------------------------------------------===//
// Instructions representing terminators
//===----------------------------------------------------------------------===//

/// This class defines a "terminating instruction" for a SILBasicBlock.
class TermInst : public SILInstruction {
protected:
  TermInst(ValueKind K, SILDebugLocation *DebugLoc)
      : SILInstruction(K, DebugLoc) {}

public:

  using ConstSuccessorListTy = ArrayRef<SILSuccessor>;
  using SuccessorListTy = MutableArrayRef<SILSuccessor>;

  /// The successor basic blocks of this terminator.
  SuccessorListTy getSuccessors();
  ConstSuccessorListTy getSuccessors() const {
    return const_cast<TermInst*>(this)->getSuccessors();
  }

  static bool classof(const ValueBase *V) {
    return V->getKind() >= ValueKind::First_TermInst &&
           V->getKind() <= ValueKind::Last_TermInst;
  }

  bool isBranch() const { return !getSuccessors().empty(); }
};

/// UnreachableInst - Position in the code which would be undefined to reach.
/// These are always implicitly generated, e.g. when falling off the end of a
/// function or after a no-return function call.
class UnreachableInst : public TermInst {
  friend class SILBuilder;

  UnreachableInst(SILDebugLocation *DebugLoc)
      : TermInst(ValueKind::UnreachableInst, DebugLoc) {}

public:
  SuccessorListTy getSuccessors() {
    // No Successors.
    return SuccessorListTy();
  }

  ArrayRef<Operand> getAllOperands() const { return {}; }
  MutableArrayRef<Operand> getAllOperands() { return {}; }

  static bool classof(const ValueBase *V) {
    return V->getKind() == ValueKind::UnreachableInst;
  }
};

/// ReturnInst - Representation of a ReturnStmt.
class ReturnInst
  : public UnaryInstructionBase<ValueKind::ReturnInst, TermInst,
                                /*HAS_RESULT*/ false>
{
  friend class SILBuilder;

  /// Constructs a ReturnInst representing a return.
  ///
  /// \param DebugLoc The backing AST location.
  ///
  /// \param ReturnValue The value to be returned.
  ///
  ReturnInst(SILDebugLocation *DebugLoc, SILValue ReturnValue)
      : UnaryInstructionBase(DebugLoc, ReturnValue) {}

public:
  SuccessorListTy getSuccessors() {
    // No Successors.
    return SuccessorListTy();
  }
};

/// AutoreleaseReturnInst - Transfer ownership of a value to an ObjC
/// autorelease pool, and then return the value.
class AutoreleaseReturnInst
  : public UnaryInstructionBase<ValueKind::AutoreleaseReturnInst, TermInst,
                                /*HAS_RESULT*/ false>
{
  friend class SILBuilder;

  /// Constructs an AutoreleaseReturnInst representing an autorelease-return
  /// sequence.
  ///
  /// \param DebugLoc The backing AST location.
  ///
  /// \param ReturnValue The value to be returned.
  ///
  AutoreleaseReturnInst(SILDebugLocation *DebugLoc, SILValue ReturnValue)
      : UnaryInstructionBase(DebugLoc, ReturnValue) {}

public:
  SuccessorListTy getSuccessors() {
    // No Successors.
    return SuccessorListTy();
  }
};

/// ThrowInst - Throw a typed error (which, in our system, is
/// essentially just a funny kind of return).
class ThrowInst
  : public UnaryInstructionBase<ValueKind::ThrowInst, TermInst,
                                /*HAS_RESULT*/ false>
{
  friend class SILBuilder;

  /// Constructs a ThrowInst representing a throw out of the current
  /// function.
  ///
  /// \param DebugLoc The location of the throw.
  /// \param errorValue The value to be thrown.
  ThrowInst(SILDebugLocation *DebugLoc, SILValue errorValue)
      : UnaryInstructionBase(DebugLoc, errorValue) {}

public:
  SuccessorListTy getSuccessors() {
    // No successors.
    return SuccessorListTy();
  }
};

/// BranchInst - An unconditional branch.
class BranchInst : public TermInst {
  friend class SILBuilder;

  SILSuccessor DestBB;
  // FIXME: probably needs dynamic adjustment
  TailAllocatedOperandList<0> Operands;

  BranchInst(SILDebugLocation *DebugLoc, SILBasicBlock *DestBB,
             ArrayRef<SILValue> Args);

  /// Construct a BranchInst that will branch to the specified block.
  /// The destination block must take no parameters.
  static BranchInst *create(SILDebugLocation *DebugLoc, SILBasicBlock *DestBB,
                            SILFunction &F);

  /// Construct a BranchInst that will branch to the specified block with
  /// the given parameters.
  static BranchInst *create(SILDebugLocation *DebugLoc, SILBasicBlock *DestBB,
                            ArrayRef<SILValue> Args, SILFunction &F);

public:
  /// \brief returns jump target for the branch.
  SILBasicBlock *getDestBB() const { return DestBB; }

  /// The arguments for the destination BB.
  OperandValueArrayRef getArgs() const { return Operands.asValueArray(); }

  SuccessorListTy getSuccessors() {
    return SuccessorListTy(&DestBB, 1);
  }

  unsigned getNumArgs() const { return Operands.size(); }
  SILValue getArg(unsigned i) const { return Operands[i].get(); }

  ArrayRef<Operand> getAllOperands() const { return Operands.asArray(); }
  MutableArrayRef<Operand> getAllOperands() { return Operands.asArray(); }

  static bool classof(const ValueBase *V) {
    return V->getKind() == ValueKind::BranchInst;
  }
};

/// A conditional branch.
class CondBranchInst : public TermInst {
  friend class SILBuilder;

public:
  enum {
    /// The operand index of the condition value used for the branch.
    ConditionIdx
  };
  enum {
    // Map branch targets to block sucessor indices.
    TrueIdx,
    FalseIdx
  };
private:
  SILSuccessor DestBBs[2];
  // The number of arguments for the True branch.
  unsigned NumTrueArgs;
  // The number of arguments for the False branch.
  unsigned NumFalseArgs;

  // The first argument is the condition; the rest are BB arguments.
  TailAllocatedOperandList<1> Operands;
  CondBranchInst(SILDebugLocation *DebugLoc, SILValue Condition,
                 SILBasicBlock *TrueBB, SILBasicBlock *FalseBB,
                 ArrayRef<SILValue> Args, unsigned NumTrue, unsigned NumFalse);

  /// Construct a CondBranchInst that will branch to TrueBB or FalseBB based on
  /// the Condition value. Both blocks must not take any arguments.
  static CondBranchInst *create(SILDebugLocation *DebugLoc, SILValue Condition,
                                SILBasicBlock *TrueBB, SILBasicBlock *FalseBB,
                                SILFunction &F);

  /// Construct a CondBranchInst that will either branch to TrueBB and pass
  /// TrueArgs or branch to FalseBB and pass FalseArgs based on the Condition
  /// value.
  static CondBranchInst *create(SILDebugLocation *DebugLoc, SILValue Condition,
                                SILBasicBlock *TrueBB,
                                ArrayRef<SILValue> TrueArgs,
                                SILBasicBlock *FalseBB,
                                ArrayRef<SILValue> FalseArgs, SILFunction &F);

public:
  SILValue getCondition() const { return Operands[ConditionIdx].get(); }
  void setCondition(SILValue newCondition) {
    Operands[ConditionIdx].set(newCondition);
  }

  SuccessorListTy getSuccessors() {
    return DestBBs;
  }

  SILBasicBlock *getTrueBB() { return DestBBs[0]; }
  const SILBasicBlock *getTrueBB() const { return DestBBs[0]; }
  SILBasicBlock *getFalseBB() { return DestBBs[1]; }
  const SILBasicBlock *getFalseBB() const { return DestBBs[1]; }

  /// Get the arguments to the true BB.
  OperandValueArrayRef getTrueArgs() const;
  /// Get the arguments to the false BB.
  OperandValueArrayRef getFalseArgs() const;

  /// Get the operands to the true BB.
  ArrayRef<Operand> getTrueOperands() const;
  MutableArrayRef<Operand> getTrueOperands();

  /// Get the operands to the false BB.
  ArrayRef<Operand> getFalseOperands() const;
  MutableArrayRef<Operand> getFalseOperands();

  /// Returns the argument on the cond_br terminator that will be passed to
  /// DestBB in A.
  SILValue getArgForDestBB(SILBasicBlock *DestBB, SILArgument *A);

  void swapSuccessors();

  ArrayRef<Operand> getAllOperands() const { return Operands.asArray(); }
  MutableArrayRef<Operand> getAllOperands() { return Operands.asArray(); }

  static bool classof(const ValueBase *V) {
    return V->getKind() == ValueKind::CondBranchInst;
  }
};

/// A switch on a value of a builtin type.
class SwitchValueInst : public TermInst {
  friend class SILBuilder;

  unsigned NumCases : 31;
  unsigned HasDefault : 1;
  TailAllocatedOperandList<1> Operands;

  SwitchValueInst(SILDebugLocation *DebugLoc, SILValue Operand,
                  SILBasicBlock *DefaultBB, ArrayRef<SILValue> Cases,
                  ArrayRef<SILBasicBlock *> BBs);

  // Tail-allocated after the SwitchValueInst record are:
  // - `NumCases` SILValue values, containing
  //   the SILValue references for each case
  // - `NumCases + HasDefault` SILSuccessor records, referencing the
  //   destinations for each case, ending with the default destination if
  //   present.


  OperandValueArrayRef getCaseBuf() const {
    return Operands.getDynamicValuesAsArray();
  }

  SILSuccessor *getSuccessorBuf() {
    return reinterpret_cast<SILSuccessor*>(Operands.asArray().end());
  }
  const SILSuccessor *getSuccessorBuf() const {
    return reinterpret_cast<const SILSuccessor *>(Operands.asArray().end());
  }

  static SwitchValueInst *
  create(SILDebugLocation *DebugLoc, SILValue Operand, SILBasicBlock *DefaultBB,
         ArrayRef<std::pair<SILValue, SILBasicBlock *>> CaseBBs,
         SILFunction &F);

public:
  /// Clean up tail-allocated successor records for the switch cases.
  ~SwitchValueInst();

  SILValue getOperand() const { return Operands[0].get(); }

  ArrayRef<Operand> getAllOperands() const { return Operands.asArray(); }
  MutableArrayRef<Operand> getAllOperands() { return Operands.asArray(); }

  SuccessorListTy getSuccessors() {
    return MutableArrayRef<SILSuccessor>{getSuccessorBuf(),
                           static_cast<size_t>(NumCases + HasDefault)};
  }

  unsigned getNumCases() const { return NumCases; }
  std::pair<SILValue, SILBasicBlock*>
  getCase(unsigned i) const {
    assert(i < NumCases && "case out of bounds");
    return {getCaseBuf()[i], getSuccessorBuf()[i]};
  }

  bool hasDefault() const { return HasDefault; }
  SILBasicBlock *getDefaultBB() const {
    assert(HasDefault && "doesn't have a default");
    return getSuccessorBuf()[NumCases];
  }

  static bool classof(const ValueBase *V) {
    return V->getKind() == ValueKind::SwitchValueInst;
  }
};

/// Common implementation for the switch_enum and
/// switch_enum_addr instructions.
class SwitchEnumInstBase : public TermInst {
  FixedOperandList<1> Operands;
  unsigned NumCases : 31;
  unsigned HasDefault : 1;

  // Tail-allocated after the SwitchEnumInst record are:
  // - an array of `NumCases` EnumElementDecl* pointers, referencing the case
  //   discriminators
  // - `NumCases + HasDefault` SILSuccessor records, referencing the
  //   destinations for each case, ending with the default destination if
  //   present.

  EnumElementDecl **getCaseBuf() {
    return reinterpret_cast<EnumElementDecl**>(this + 1);

  }
  EnumElementDecl * const* getCaseBuf() const {
    return reinterpret_cast<EnumElementDecl* const*>(this + 1);

  }

  SILSuccessor *getSuccessorBuf() {
    return reinterpret_cast<SILSuccessor*>(getCaseBuf() + NumCases);
  }
  const SILSuccessor *getSuccessorBuf() const {
    return reinterpret_cast<const SILSuccessor*>(getCaseBuf() + NumCases);
  }

protected:
  SwitchEnumInstBase(
      ValueKind Kind, SILDebugLocation *DebugLoc, SILValue Operand,
      SILBasicBlock *DefaultBB,
      ArrayRef<std::pair<EnumElementDecl *, SILBasicBlock *>> CaseBBs);

  template <typename SWITCH_ENUM_INST>
  static SWITCH_ENUM_INST *createSwitchEnum(
      SILDebugLocation *DebugLoc, SILValue Operand, SILBasicBlock *DefaultBB,
      ArrayRef<std::pair<EnumElementDecl *, SILBasicBlock *>> CaseBBs,
      SILFunction &F);

public:
  /// Clean up tail-allocated successor records for the switch cases.
  ~SwitchEnumInstBase();

  SILValue getOperand() const { return Operands[0].get(); }

  ArrayRef<Operand> getAllOperands() const { return Operands.asArray(); }
  MutableArrayRef<Operand> getAllOperands() { return Operands.asArray(); }

  SuccessorListTy getSuccessors() {
    return MutableArrayRef<SILSuccessor>{getSuccessorBuf(),
                           static_cast<size_t>(NumCases + HasDefault)};
  }

  unsigned getNumCases() const { return NumCases; }
  std::pair<EnumElementDecl*, SILBasicBlock*>
  getCase(unsigned i) const {
    assert(i < NumCases && "case out of bounds");
    return {getCaseBuf()[i], getSuccessorBuf()[i].getBB()};
  }

  /// \brief Return the block that will be branched to on the specified enum
  /// case.
  SILBasicBlock *getCaseDestination(EnumElementDecl *D) {
    for (unsigned i = 0, e = getNumCases(); i != e; ++i) {
      auto Entry = getCase(i);
      if (Entry.first == D) return Entry.second;
    }
    // switch_enum is required to be fully covered, so return the default if we
    // didn't find anything.
    return getDefaultBB();
  }

  /// \brief If the default refers to exactly one case decl, return it.
  NullablePtr<EnumElementDecl> getUniqueCaseForDefault();

  /// \brief If the given block only has one enum element decl matched to it,
  /// return it.
  NullablePtr<EnumElementDecl> getUniqueCaseForDestination(SILBasicBlock *BB);

  bool hasDefault() const { return HasDefault; }
  SILBasicBlock *getDefaultBB() const {
    assert(HasDefault && "doesn't have a default");
    return getSuccessorBuf()[NumCases];
  }

  static bool classof(const ValueBase *V) {
    return V->getKind() >= ValueKind::SwitchEnumInst &&
           V->getKind() <= ValueKind::SwitchEnumAddrInst;
  }
};

/// A switch on a loadable enum's discriminator. The data for each case is
/// passed into the corresponding destination block as an argument.
class SwitchEnumInst : public SwitchEnumInstBase {
  friend class SILBuilder;

private:
  friend class SwitchEnumInstBase;

  SwitchEnumInst(
      SILDebugLocation *DebugLoc, SILValue Operand, SILBasicBlock *DefaultBB,
      ArrayRef<std::pair<EnumElementDecl *, SILBasicBlock *>> CaseBBs)
      : SwitchEnumInstBase(ValueKind::SwitchEnumInst, DebugLoc, Operand,
                           DefaultBB, CaseBBs) {}

  static SwitchEnumInst *
  create(SILDebugLocation *DebugLoc, SILValue Operand, SILBasicBlock *DefaultBB,
         ArrayRef<std::pair<EnumElementDecl *, SILBasicBlock *>> CaseBBs,
         SILFunction &F);

public:
  static bool classof(const ValueBase *V) {
    return V->getKind() == ValueKind::SwitchEnumInst;
  }
};

/// A switch on an enum's discriminator in memory.
class SwitchEnumAddrInst : public SwitchEnumInstBase {
  friend class SILBuilder;

private:
  friend class SwitchEnumInstBase;

  SwitchEnumAddrInst(
      SILDebugLocation *DebugLoc, SILValue Operand, SILBasicBlock *DefaultBB,
      ArrayRef<std::pair<EnumElementDecl *, SILBasicBlock *>> CaseBBs)
      : SwitchEnumInstBase(ValueKind::SwitchEnumAddrInst, DebugLoc, Operand,
                           DefaultBB, CaseBBs) {}

  static SwitchEnumAddrInst *
  create(SILDebugLocation *DebugLoc, SILValue Operand, SILBasicBlock *DefaultBB,
         ArrayRef<std::pair<EnumElementDecl *, SILBasicBlock *>> CaseBBs,
         SILFunction &F);

public:
  static bool classof(const ValueBase *V) {
    return V->getKind() == ValueKind::SwitchEnumAddrInst;
  }
};

/// Branch on the existence of an Objective-C method in the dynamic type of
/// an object.
///
/// If the method exists, branches to the first BB, providing it with the
/// method reference; otherwise, branches to the second BB.
class DynamicMethodBranchInst : public TermInst {
  friend class SILBuilder;

  SILDeclRef Member;

  SILSuccessor DestBBs[2];

  // The operand.
  FixedOperandList<1> Operands;

  DynamicMethodBranchInst(SILDebugLocation *DebugLoc, SILValue Operand,
                          SILDeclRef Member, SILBasicBlock *HasMethodBB,
                          SILBasicBlock *NoMethodBB);

  /// Construct a DynamicMethodBranchInst that will branch to \c HasMethodBB or
  /// \c NoMethodBB based on the ability of the object operand to respond to
  /// a message with the same selector as the member.
  static DynamicMethodBranchInst *
  create(SILDebugLocation *DebugLoc, SILValue Operand, SILDeclRef Member,
         SILBasicBlock *HasMethodBB, SILBasicBlock *NoMethodBB, SILFunction &F);

public:
  SILValue getOperand() const { return Operands[0].get(); }

  SILDeclRef getMember() const { return Member; }

  SuccessorListTy getSuccessors() {
    return DestBBs;
  }

  SILBasicBlock *getHasMethodBB() { return DestBBs[0]; }
  const SILBasicBlock *getHasMethodBB() const { return DestBBs[0]; }
  SILBasicBlock *getNoMethodBB() { return DestBBs[1]; }
  const SILBasicBlock *getNoMethodBB() const { return DestBBs[1]; }

  ArrayRef<Operand> getAllOperands() const { return Operands.asArray(); }
  MutableArrayRef<Operand> getAllOperands() { return Operands.asArray(); }

  static bool classof(const ValueBase *V) {
    return V->getKind() == ValueKind::DynamicMethodBranchInst;
  }
};

/// Perform a checked cast operation and branch on whether the cast succeeds.
/// The success branch destination block receives the cast result as a BB
/// argument.
class CheckedCastBranchInst : public TermInst {
  friend class SILBuilder;

  SILType DestTy;
  bool IsExact;

  FixedOperandList<1> Operands;
  SILSuccessor DestBBs[2];

  CheckedCastBranchInst(SILDebugLocation *DebugLoc, bool IsExact,
                        SILValue Operand, SILType DestTy,
                        SILBasicBlock *SuccessBB, SILBasicBlock *FailureBB)
      : TermInst(ValueKind::CheckedCastBranchInst, DebugLoc), DestTy(DestTy),
        IsExact(IsExact), Operands{this, Operand},
        DestBBs{{this, SuccessBB}, {this, FailureBB}} {}

public:
  ArrayRef<Operand> getAllOperands() const { return Operands.asArray(); }
  MutableArrayRef<Operand> getAllOperands() { return Operands.asArray(); }

  SILValue getOperand() const { return Operands[0].get(); }
  bool isExact() const { return IsExact; }

  SuccessorListTy getSuccessors() {
    return DestBBs;
  }

  SILType getCastType() const { return DestTy; }

  SILBasicBlock *getSuccessBB() { return DestBBs[0]; }
  const SILBasicBlock *getSuccessBB() const { return DestBBs[0]; }
  SILBasicBlock *getFailureBB() { return DestBBs[1]; }
  const SILBasicBlock *getFailureBB() const { return DestBBs[1]; }

  static bool classof(const ValueBase *V) {
    return V->getKind() == ValueKind::CheckedCastBranchInst;
  }
};

/// Perform a checked cast operation and branch on whether the cast succeeds.
/// The result of the checked cast is left in the destination address.
class CheckedCastAddrBranchInst : public TermInst {
  friend class SILBuilder;

  CastConsumptionKind ConsumptionKind;

  enum {
    /// the value being stored
    Src,
    /// the lvalue being stored to
    Dest
  };
  FixedOperandList<2> Operands;
  SILSuccessor DestBBs[2];

  CanType SourceType;
  CanType TargetType;

  CheckedCastAddrBranchInst(SILDebugLocation *DebugLoc,
                            CastConsumptionKind consumptionKind, SILValue src,
                            CanType srcType, SILValue dest, CanType targetType,
                            SILBasicBlock *successBB, SILBasicBlock *failureBB)
      : TermInst(ValueKind::CheckedCastAddrBranchInst, DebugLoc),
        ConsumptionKind(consumptionKind), Operands{this, src, dest},
        DestBBs{{this, successBB}, {this, failureBB}}, SourceType(srcType),
        TargetType(targetType) {}

public:
  CastConsumptionKind getConsumptionKind() const { return ConsumptionKind; }

  SILValue getSrc() const { return Operands[Src].get(); }
  SILValue getDest() const { return Operands[Dest].get(); }

  /// Returns the formal type of the source value.
  CanType getSourceType() const { return SourceType; }

  /// Returns the formal target type.
  CanType getTargetType() const { return TargetType; }

  ArrayRef<Operand> getAllOperands() const { return Operands.asArray(); }
  MutableArrayRef<Operand> getAllOperands() { return Operands.asArray(); }

  SuccessorListTy getSuccessors() {
    return DestBBs;
  }

  SILBasicBlock *getSuccessBB() { return DestBBs[0]; }
  const SILBasicBlock *getSuccessBB() const { return DestBBs[0]; }
  SILBasicBlock *getFailureBB() { return DestBBs[1]; }
  const SILBasicBlock *getFailureBB() const { return DestBBs[1]; }

  static bool classof(const ValueBase *V) {
    return V->getKind() == ValueKind::CheckedCastAddrBranchInst;
  }
};

/// A private abstract class to store the destinations of a TryApplyInst.
class TryApplyInstBase : public TermInst {
public:
  enum {
    // Map branch targets to block sucessor indices.
    NormalIdx,
    ErrorIdx
  };
private:
  SILSuccessor DestBBs[2];

protected:
  TryApplyInstBase(ValueKind valueKind, SILDebugLocation *Loc,
                   SILBasicBlock *normalBB, SILBasicBlock *errorBB);

public:
  SuccessorListTy getSuccessors() {
    return DestBBs;
  }

  bool isNormalSuccessorRef(SILSuccessor *successor) const {
    assert(successor == &DestBBs[0] || successor == &DestBBs[1]);
    return successor == &DestBBs[0];
  }
  bool isErrorSuccessorRef(SILSuccessor *successor) const {
    assert(successor == &DestBBs[0] || successor == &DestBBs[1]);
    return successor == &DestBBs[1];
  }  

  SILBasicBlock *getNormalBB() { return DestBBs[NormalIdx]; }
  const SILBasicBlock *getNormalBB() const { return DestBBs[NormalIdx]; }
  SILBasicBlock *getErrorBB() { return DestBBs[ErrorIdx]; }
  const SILBasicBlock *getErrorBB() const { return DestBBs[ErrorIdx]; }
};

/// TryApplyInst - Represents the full application of a function that
/// can produce an error.
class TryApplyInst
    : public ApplyInstBase<TryApplyInst, TryApplyInstBase> {
  friend class SILBuilder;

  TryApplyInst(SILDebugLocation *DebugLoc, SILValue callee,
               SILType substCalleeType, ArrayRef<Substitution> substitutions,
               ArrayRef<SILValue> args, SILBasicBlock *normalBB,
               SILBasicBlock *errorBB);

  static TryApplyInst *create(SILDebugLocation *DebugLoc, SILValue callee,
                              SILType substCalleeType,
                              ArrayRef<Substitution> substitutions,
                              ArrayRef<SILValue> args, SILBasicBlock *normalBB,
                              SILBasicBlock *errorBB, SILFunction &F);

public:
  static bool classof(const ValueBase *V) {
    return V->getKind() == ValueKind::TryApplyInst;
  }
};

/// An apply instruction.
class ApplySite {
  SILInstruction *Inst;

protected:
  explicit ApplySite(void *p) : Inst(static_cast<SILInstruction *>(p)) {}

public:
  ApplySite() : Inst(nullptr) {}
  explicit ApplySite(ValueBase *inst)
    : Inst(static_cast<SILInstruction*>(inst)) {
    assert(classof(inst) && "not an apply instruction?");
  }
  ApplySite(ApplyInst *inst) : Inst(inst) {}
  ApplySite(PartialApplyInst *inst) : Inst(inst) {}
  ApplySite(TryApplyInst *inst) : Inst(inst) {}

  SILModule &getModule() const {
    return Inst->getModule();
  }

  static ApplySite isa(ValueBase *inst) {
    return (classof(inst) ? ApplySite(inst) : ApplySite());
  }

  explicit operator bool() const {
    return Inst != nullptr;
  }

  SILInstruction *getInstruction() const { return Inst; }
  SILLocation getLoc() const { return Inst->getLoc(); }
  const SILDebugScope *getDebugScope() const { return Inst->getDebugScope(); }
  SILFunction *getFunction() const { return Inst->getFunction(); }
  SILBasicBlock *getParent() const { return Inst->getParent(); }

#define FOREACH_IMPL_RETURN(OPERATION) do {                             \
    switch (Inst->getKind()) {                                          \
    case ValueKind::ApplyInst:                                          \
      return cast<ApplyInst>(Inst)->OPERATION;                          \
    case ValueKind::PartialApplyInst:                                   \
      return cast<PartialApplyInst>(Inst)->OPERATION;                   \
    case ValueKind::TryApplyInst:                                       \
      return cast<TryApplyInst>(Inst)->OPERATION;                       \
    default:                                                            \
      llvm_unreachable("not an apply instruction!");                    \
    }                                                                   \
  } while(0)

  /// Return the callee operand.
  SILValue getCallee() const {
    FOREACH_IMPL_RETURN(getCallee());
  }

  // Return the referenced function if the callee is a function_ref instruction.
  SILFunction *getCalleeFunction() const {
    FOREACH_IMPL_RETURN(getCalleeFunction());
  }

  /// Return the type.
  SILType getType() const {
    FOREACH_IMPL_RETURN(getSubstCalleeType()->getResult().getSILType());
  }

  /// Get the type of the callee without the applied substitutions.
  CanSILFunctionType getOrigCalleeType() const {
    return getCallee().getType().castTo<SILFunctionType>();
  }

  /// Get the type of the callee with the applied substitutions.
  CanSILFunctionType getSubstCalleeType() const {
    return getSubstCalleeSILType().castTo<SILFunctionType>();
  }
  SILType getSubstCalleeSILType() const {
    FOREACH_IMPL_RETURN(getSubstCalleeSILType());
  }

  bool isCalleeThin() const {
    switch (getSubstCalleeType()->getRepresentation()) {
    case SILFunctionTypeRepresentation::CFunctionPointer:
    case SILFunctionTypeRepresentation::Thin:
    case SILFunctionTypeRepresentation::Method:
    case SILFunctionTypeRepresentation::ObjCMethod:
    case SILFunctionTypeRepresentation::WitnessMethod:
      return true;
    case SILFunctionTypeRepresentation::Block:
    case SILFunctionTypeRepresentation::Thick:
      return false;
    }
  }

  /// True if this application has generic substitutions.
  bool hasSubstitutions() const {
    FOREACH_IMPL_RETURN(hasSubstitutions());
  }

  /// The substitutions used to bind the generic arguments of this function.
  MutableArrayRef<Substitution> getSubstitutions() const {
    FOREACH_IMPL_RETURN(getSubstitutions());
  }

  ArrayRef<Substitution> getSubstitutionsWithoutSelfSubstitution() const {
    switch (Inst->getKind()) {
    case ValueKind::ApplyInst:
      return cast<ApplyInst>(Inst)->getSubstitutionsWithoutSelfSubstitution();
    case ValueKind::TryApplyInst:
      return cast<TryApplyInst>(Inst)->getSubstitutionsWithoutSelfSubstitution();
    default:
      llvm_unreachable("not implemented for this instruction!");
    }
  }

  /// The arguments passed to this instruction.
  MutableArrayRef<Operand> getArgumentOperands() const {
    FOREACH_IMPL_RETURN(getArgumentOperands());
  }

  /// The arguments passed to this instruction.
  OperandValueArrayRef getArguments() const {
    FOREACH_IMPL_RETURN(getArguments());
  }

  /// The arguments passed to this instruction, without self.
  OperandValueArrayRef getArgumentsWithoutSelf() const {
    switch (Inst->getKind()) {
    case ValueKind::ApplyInst:
      return cast<ApplyInst>(Inst)->getArgumentsWithoutSelf();
    case ValueKind::TryApplyInst:
      return cast<TryApplyInst>(Inst)->getArgumentsWithoutSelf();
    default:
      llvm_unreachable("not implemented for this instruction!");
    }
  }

  /// Returns the number of arguments for this partial apply.
  unsigned getNumArguments() const { return getArguments().size(); }

  Operand &getArgumentRef(unsigned i) const { return getArgumentOperands()[i]; }

  /// Return the ith argument passed to this instruction.
  SILValue getArgument(unsigned i) const { return getArguments()[i]; }

  // Set the ith argument of this instruction.
  void setArgument(unsigned i, SILValue V) const {
    getArgumentOperands()[i].set(V);
  }
#undef FOREACH_IMPL_RETURN

  static ApplySite getFromOpaqueValue(void *p) {
    return ApplySite(p);
  }

  friend bool operator==(ApplySite lhs, ApplySite rhs) {
    return lhs.getInstruction() == rhs.getInstruction();
  }
  friend bool operator!=(ApplySite lhs, ApplySite rhs) {
    return lhs.getInstruction() != rhs.getInstruction();
  }

  static bool classof(const ValueBase *inst) {
    return (inst->getKind() == ValueKind::ApplyInst ||
            inst->getKind() == ValueKind::PartialApplyInst ||
            inst->getKind() == ValueKind::TryApplyInst);
  }
};

/// A full function application.
class FullApplySite : public ApplySite {
  explicit FullApplySite(void *p) : ApplySite(p) {}

public:
  FullApplySite() : ApplySite() {}
  explicit FullApplySite(ValueBase *inst) : ApplySite(inst) {
    assert(classof(inst) && "not an apply instruction?");
  }
  FullApplySite(ApplyInst *inst) : ApplySite(inst) {}
  FullApplySite(TryApplyInst *inst) : ApplySite(inst) {}

  static FullApplySite isa(ValueBase *inst) {
    return (classof(inst) ? FullApplySite(inst) : FullApplySite());
  }

  bool hasIndirectResult() const {
    return getSubstCalleeType()->hasIndirectResult();
  }

  SILValue getIndirectResult() const {
    assert(hasIndirectResult() && "apply inst does not have indirect result!");
    return getArguments().front();
  }

  OperandValueArrayRef getArgumentsWithoutIndirectResult() const {
    if (hasIndirectResult())
      return getArguments().slice(1);
    return getArguments();
  }

  static FullApplySite getFromOpaqueValue(void *p) {
    return FullApplySite(p);
  }

  static bool classof(const ValueBase *inst) {
    return (inst->getKind() == ValueKind::ApplyInst ||
            inst->getKind() == ValueKind::TryApplyInst);
  }
};

} // end swift namespace

//===----------------------------------------------------------------------===//
// ilist_traits for SILInstruction
//===----------------------------------------------------------------------===//

namespace llvm {

template <>
struct ilist_traits<::swift::SILInstruction> :
  public ilist_default_traits<::swift::SILInstruction> {
  using SILInstruction = ::swift::SILInstruction;

private:
  mutable ilist_half_node<SILInstruction> Sentinel;

  swift::SILBasicBlock *getContainingBlock();

public:
  SILInstruction *createSentinel() const {
    return static_cast<SILInstruction*>(&Sentinel);
  }
  void destroySentinel(SILInstruction *) const {}

  SILInstruction *provideInitialHead() const { return createSentinel(); }
  SILInstruction *ensureHead(SILInstruction*) const { return createSentinel(); }
  static void noteHead(SILInstruction*, SILInstruction*) {}
  static void deleteNode(SILInstruction *V) {
    SILInstruction::destroy(V);
  }

  void addNodeToList(SILInstruction *I);
  void removeNodeFromList(SILInstruction *I);
  void transferNodesFromList(ilist_traits<SILInstruction> &L2,
                             ilist_iterator<SILInstruction> first,
                             ilist_iterator<SILInstruction> last);

private:
  void createNode(const SILInstruction &);
};

// An ApplySite casts like a SILInstruction*.
template<> struct simplify_type<const ::swift::ApplySite> {
  using SimpleType = ::swift::SILInstruction *;
  static SimpleType getSimplifiedValue(const ::swift::ApplySite &Val) {
    return Val.getInstruction();
  }
};
template<> struct simplify_type< ::swift::ApplySite>
  : public simplify_type<const ::swift::ApplySite> {};
template<> struct simplify_type< ::swift::FullApplySite>
  : public simplify_type<const ::swift::ApplySite> {};
template<> struct simplify_type<const ::swift::FullApplySite>
  : public simplify_type<const ::swift::ApplySite> {};


template<> struct DenseMapInfo< ::swift::ApplySite> {
  static ::swift::ApplySite getEmptyKey() {
    return ::swift::ApplySite::getFromOpaqueValue(
      llvm::DenseMapInfo<void *>::getEmptyKey());
  }
  static ::swift::ApplySite getTombstoneKey() {
    return ::swift::ApplySite::getFromOpaqueValue(
      llvm::DenseMapInfo<void *>::getTombstoneKey());
  }
  static unsigned getHashValue( ::swift::ApplySite AS) {
    auto *I = AS.getInstruction();
    return DenseMapInfo< ::swift::SILInstruction *>::getHashValue(I);
  }
  static bool isEqual( ::swift::ApplySite LHS, ::swift::ApplySite RHS) {
    return LHS == RHS;
  }
};

template<> struct DenseMapInfo< ::swift::FullApplySite> {
  static ::swift::FullApplySite getEmptyKey() {
    return ::swift::FullApplySite::getFromOpaqueValue(
      llvm::DenseMapInfo<void*>::getEmptyKey());
  }
  static ::swift::FullApplySite getTombstoneKey() {
    return ::swift::FullApplySite::getFromOpaqueValue(
      llvm::DenseMapInfo<void*>::getTombstoneKey());
  }
  static unsigned getHashValue( ::swift::FullApplySite AS) {
    auto *I = AS.getInstruction();
    return DenseMapInfo< ::swift::SILInstruction *>::getHashValue(I);
  }
  static bool isEqual( ::swift::FullApplySite LHS, ::swift::FullApplySite RHS) {
    return LHS == RHS;
  }
};

} // end llvm namespace

#endif
