//===--- Instruction.h - Instructions for high-level CFGs --------*- C++ -*-==//
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
// This file defines the high-level Instruction class used for Swift CFGs.
//
//===----------------------------------------------------------------------===//

#ifndef SWIFT_CFG_INSTRUCTION_H
#define SWIFT_CFG_INSTRUCTION_H

#include "swift/Basic/LLVM.h"
#include "swift/CFG/CFGBase.h"
#include "swift/CFG/CFGLocation.h"
#include "swift/CFG/CFGSuccessor.h"
#include "swift/CFG/Value.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/ilist_node.h"
#include "llvm/ADT/ilist.h"

namespace swift {

class ValueDecl;
class Type;
class CFG;
class BasicBlock;
class ApplyExpr;
class AssignStmt;
class CharacterLiteralExpr;
class DeclRefExpr;
class FloatLiteralExpr;
class ImplicitConversionExpr;
class IntegerLiteralExpr;
class LoadExpr;
class MaterializeExpr;
class ReturnStmt;
class ScalarToTupleExpr;
class StringLiteralExpr;
class Stmt;
class TupleElementExpr;
class TupleExpr;
class TupleShuffleExpr;
class TypeOfExpr;
class VarDecl;

/// This is the root class for all instructions that can be used as the contents
/// of a Swift BasicBlock.
class Instruction : public Value, public llvm::ilist_node<Instruction> {
  friend struct llvm::ilist_traits<Instruction>;

  /// A backreference to the containing basic block.  This is maintained by
  /// ilist_traits<Instruction>.
  BasicBlock *ParentBB;

  CFGLocation Loc;

  friend struct llvm::ilist_sentinel_traits<Instruction>;
  Instruction() = delete;
  void operator=(const Instruction &) = delete;
  void operator delete(void *Ptr, size_t) = delete;

protected:
  Instruction(ValueKind Kind, CFGLocation Loc, Type Ty)
    : Value(Kind, Ty), ParentBB(0), Loc(Loc) {}

public:

  const BasicBlock *getParent() const { return ParentBB; }
  BasicBlock *getParent() { return ParentBB; }

  CFGLocation getLoc() const { return Loc; }

  /// Return the AST expression that this instruction is produced from, or null
  /// if it is implicitly generated.  Note that this is aborts on locations that
  /// come from statements.
  template<typename T>
  T *getLocDecl() const { return cast_or_null<T>(Loc.template get<Decl*>()); }

  /// Return the AST expression that this instruction is produced from, or null
  /// if it is implicitly generated.  Note that this is aborts on locations that
  /// come from statements.
  template<typename T>
  T *getLocExpr() const { return cast_or_null<T>(Loc.template get<Expr*>()); }

  /// Return the AST statement that this instruction is produced from, or null
  /// if it is implicitly generated.  Note that this is aborts on locations that
  /// come from statements.
  template<typename T>
  T *getLocStmt() const { return cast_or_null<T>(Loc.template get<Stmt*>()); }


  /// removeFromParent - This method unlinks 'this' from the containing basic
  /// block, but does not delete it.
  ///
  void removeFromParent();
  
  /// eraseFromParent - This method unlinks 'this' from the containing basic
  /// block and deletes it.
  ///
  void eraseFromParent();

  static bool classof(const Value *I) {
    return I->getKind() >= ValueKind::First_InstructionInst &&
           I->getKind() <= ValueKind::Last_InstructionInst;
  }
};


/// AllocInst - This is the abstract base class common among all the memory
/// allocation mechanisms.  This can allocate heap or stack memory.
class AllocInst : public Instruction {
// Eventually: enum AllocKind { Heap, Stack, StackNoRefCount, Pseudo };

protected:
  AllocInst(ValueKind Kind, CFGLocation Loc, Type Ty)
    : Instruction(Kind, Loc, Ty) {}
public:

  static bool classof(const Value *I) {
    return I->getKind() >= ValueKind::First_AllocInst &&
           I->getKind() <= ValueKind::Last_AllocInst;
  }
};


/// AllocVarInst - This represents the allocation of a local variable due to a
/// 'var' declaration.  A single var declaration may allocate multiple different
/// CFG variables at once through its pattern.  One of these will be created
/// for each variable in something like "var (x,y) : (Int, Int)".
class AllocVarInst : public AllocInst {
public:
  AllocVarInst(VarDecl *VD);

  /// getDecl - Return the underlying declaration.
  VarDecl *getDecl() const;

  static bool classof(const Value *I) {
    return I->getKind() == ValueKind::AllocVar;
  }
};

/// AllocTmpInst - This represents the allocation of a temporary variable due to a
/// a MaterializeExpr.  This occurs when an rvalue needs to be converted to an
/// l-value, for example to be the receiver of a dot-syntax method call.
///
/// The initial value for the temp will be provided by an initalization-style
/// store to the temporary.
class AllocTmpInst : public AllocInst {
public:

  AllocTmpInst(MaterializeExpr *E);

  static bool classof(const Value *I) {
    return I->getKind() == ValueKind::AllocTmp;
  }
};

/// AllocArrayInst - This represents the allocation of an array of elements,
/// whose element memory is left uninitialized.  This returns a value of tuple
/// type.  The first return element is the object pointer (pointer to the object
/// header) with Builtin.ObjectPointer type.  The second element returned is an
/// lvalue to the first array element.
///
class AllocArrayInst : public Instruction {
  Type ElementType;
  unsigned NumElements;
public:

  AllocArrayInst(TupleShuffleExpr *E, Type ElementType, unsigned NumElements);

  Type getElementType() const { return ElementType; }
  unsigned getNumElements() const { return NumElements; }

  static bool classof(const Value *I) {
    return I->getKind() == ValueKind::AllocArray;
  }
};


/// ApplyInst - Represents application of an argument to a function.
class ApplyInst : public Instruction {
  /// The instruction representing the called function.
  CFGValue Callee;

  unsigned NumArgs;
  CFGValue *getArgsStorage() { return reinterpret_cast<CFGValue*>(this + 1); }
  
  /// Construct an ApplyInst from a given call expression and the provided
  /// arguments.
  ApplyInst(ApplyExpr *Expr, CFGValue Callee, ArrayRef<CFGValue> Args);

public:
  static ApplyInst *create(ApplyExpr *Expr, CFGValue Callee,
                          ArrayRef<CFGValue> Args, CFG &C);

  
  CFGValue getCallee() { return Callee; }
  
  /// The arguments passed to this ApplyInst.
  MutableArrayRef<CFGValue> getArguments() {
    return MutableArrayRef<CFGValue>(getArgsStorage(), NumArgs);
  }

  /// The arguments passed to this ApplyInst.
  ArrayRef<CFGValue> getArguments() const {
    return const_cast<ApplyInst*>(this)->getArguments();
  }

  static bool classof(const Value *I) {
    return I->getKind() == ValueKind::Apply;
  }
};

/// ConstantRefInst - Represents a reference to a *constant* declaration,
/// evaluating to its value.
class ConstantRefInst : public Instruction {
public:

  /// Construct a ConstantRefInst.
  ///
  /// \param Expr A backpointer to the original DeclRefExpr.
  ///
  ConstantRefInst(DeclRefExpr *E);

  DeclRefExpr *getExpr() const;

  /// getDecl - Return the underlying declaration.
  ValueDecl *getDecl() const;

  static bool classof(const Value *I) {
    return I->getKind() == ValueKind::ConstantRef;
  }
};

/// A default "zero" value used to initialize a variable that was not otherwise
/// explicitly initialized.
class ZeroValueInst : public Instruction {
public:
  ZeroValueInst(VarDecl *D);

  static bool classof(const Value *I) {
    return I->getKind() == ValueKind::ZeroValue;
  }
};

/// IntegerLiteralInst - Encapsulates an integer constant, as defined originally
/// by an an IntegerLiteralExpr.
class IntegerLiteralInst : public Instruction {
public:
  IntegerLiteralInst(IntegerLiteralExpr *E);
  
  IntegerLiteralExpr *getExpr() const;
  
  /// getValue - Return the APInt for the underlying integer literal.
  APInt getValue() const;

  static bool classof(const Value *I) {
    return I->getKind() == ValueKind::IntegerLiteral;
  }
};

/// FloatLiteralInst - Encapsulates a floating point constant, as defined
/// originally by a FloatLiteralExpr.
class FloatLiteralInst : public Instruction {
public:
  FloatLiteralInst(FloatLiteralExpr *E);

  FloatLiteralExpr *getExpr() const;

  /// getValue - Return the APFloat for the underlying FP literal.
  APFloat getValue() const;

  static bool classof(const Value *I) {
    return I->getKind() == ValueKind::FloatLiteral;
  }
};

/// CharacterLiteralInst - Encapsulates a character constant, as defined
/// originally by a CharacterLiteralExpr.
class CharacterLiteralInst : public Instruction {
public:
  CharacterLiteralInst(CharacterLiteralExpr *E);

  CharacterLiteralExpr *getExpr() const;

  /// getValue - Return the value for the underlying literal.
  uint32_t getValue() const;

  static bool classof(const Value *I) {
    return I->getKind() == ValueKind::CharacterLiteral;
  }
};

/// StringLiteralInst - Encapsulates a string constant, as defined originally by
/// a StringLiteralExpr.
class StringLiteralInst : public Instruction {
public:
  StringLiteralInst(StringLiteralExpr *E);

  StringLiteralExpr *getExpr() const;

  /// getValue - Return the string data for the literal.
  StringRef getValue() const;

  static bool classof(const Value *I) {
    return I->getKind() == ValueKind::StringLiteral;
  }
};


/// LoadInst - Represents a load from a memory location.
class LoadInst : public Instruction {
  /// The LValue (memory address) to use for the load.
  CFGValue LValue;
public:
  /// Constructs a LoadInst.
  ///
  /// \param Expr The backing LoadExpr in the AST.
  ///
  /// \param LValue The CFGValue representing the lvalue (address) to
  ///        use for the load.
  ///
  LoadInst(LoadExpr *E, CFGValue LValue);

  CFGValue getLValue() const { return LValue; }

  static bool classof(const Value *I) {
    return I->getKind() == ValueKind::Load;
  }
};

class StoreInst : public Instruction {
  /// The value being stored and the lvalue being stored to.
  CFGValue Src, Dest;

  /// IsInitialization - True if this is the initialization of a memory location
  /// that is uninitialized, not a general store.  In an initialization of an
  /// ARC'd pointer (for example), the old value is not released.
  bool IsInitialization;
public:

  StoreInst(AssignStmt *S, CFGValue Src, CFGValue Dest);
  StoreInst(VarDecl *VD, CFGValue Src, CFGValue Dest);
  StoreInst(MaterializeExpr *E, CFGValue Src, CFGValue Dest);
  StoreInst(TupleShuffleExpr *E, CFGValue Src, CFGValue Dest);

  CFGValue getSrc() const { return Src; }
  CFGValue getDest() const { return Dest; }

  bool isInitialization() const { return IsInitialization; }

  static bool classof(const Value *I) {
    return I->getKind() == ValueKind::Store;
  }
};

/// TypeConversionInst - Change the Type of some value without affecting how it
/// will codegen.
class TypeConversionInst : public Instruction {
  CFGValue Operand;
public:
  TypeConversionInst(ImplicitConversionExpr *E, CFGValue Operand);

  CFGValue getOperand() const { return Operand; }
  
  static bool classof(const Value *I) {
    return I->getKind() == ValueKind::TypeConversion;
  }
};


/// TupleInst - Represents a constructed tuple.
class TupleInst : public Instruction {
  CFGValue *getElementsStorage() {
    return reinterpret_cast<CFGValue*>(this + 1);
  }
  unsigned NumArgs;

  /// Private constructor.  Because of the storage requirements of
  /// TupleInst, object creation goes through 'create()'.
  TupleInst(Expr *E, ArrayRef<CFGValue> Elements);
  static TupleInst *createImpl(Expr *E, ArrayRef<CFGValue> Elements, CFG &C);

public:
  /// The elements referenced by this TupleInst.
  MutableArrayRef<CFGValue> getElements() {
    return MutableArrayRef<CFGValue>(getElementsStorage(), NumArgs);
  }

  /// The elements referenced by this TupleInst.
  ArrayRef<CFGValue> getElements() const {
    return const_cast<TupleInst*>(this)->getElements();
  }

  /// Construct a TupleInst.  The two forms are used to ensure that these are
  /// only created for specific syntactic forms.
  static TupleInst *create(TupleExpr *E, ArrayRef<CFGValue> Elements,CFG &C){
    return createImpl((Expr*)E, Elements, C);
  }
  static TupleInst *create(TupleShuffleExpr *E, ArrayRef<CFGValue> Elements,
                           CFG &C) {
    return createImpl((Expr*)E, Elements, C);
  }

  static bool classof(const Value *I) {
    return I->getKind() == ValueKind::Tuple;
  }
};

/// TypeOfInst - Represents the production of an instance of a given metatype.
class TypeOfInst : public Instruction {
public:

  /// Constructs a TypeOfInst.
  ///
  /// \param Expr A backpointer to the original TypeOfExpr.
  ///
  TypeOfInst(TypeOfExpr *E);

  TypeOfExpr *getExpr() const;

  /// getMetaType - Return the type of the metatype that this instruction
  /// returns.
  Type getMetaType() const;

  static bool classof(const Value *I) {
    return I->getKind() == ValueKind::TypeOf;
  }
};

/// ScalarToTupleInst - Convert a scalar to a tuple.
class ScalarToTupleInst : public Instruction {
  CFGValue Operand;
public:
  ScalarToTupleInst(ScalarToTupleExpr *E, CFGValue Operand);

  CFGValue getOperand() const { return Operand; }

  static bool classof(const Value *I) {
    return I->getKind() == ValueKind::ScalarToTuple;
  }
};
  
/// TupleElementInst - Extract a numbered element out of a value of tuple type.
class TupleElementInst : public Instruction {
  CFGValue Operand;
  unsigned FieldNo;
public:
  TupleElementInst(TupleElementExpr *E, CFGValue Operand, unsigned FieldNo);
  TupleElementInst(Type ResultTy, CFGValue Operand, unsigned FieldNo);
  
  CFGValue getOperand() const { return Operand; }
  unsigned getFieldNo() const { return FieldNo; }
  
  static bool classof(const Value *I) {
    return I->getKind() == ValueKind::TupleElement;
  }
};

//===----------------------------------------------------------------------===//
// CFG-only instructions that don't have an AST analog
//===----------------------------------------------------------------------===//


/// IndexLValueInst - "%1 = index_lvalue %0, 42"
/// This takes an lvalue and indexes over the pointer, striding by the type of
/// the lvalue.  This is used to index into arrays of uniform elements.
class IndexLValueInst : public Instruction {
  CFGValue Operand;
  unsigned Index;
public:
  IndexLValueInst(TupleShuffleExpr *E, CFGValue Operand, unsigned Index);

  CFGValue getOperand() const { return Operand; }
  unsigned getIndex() const { return Index; }

  static bool classof(const Value *I) {
    return I->getKind() == ValueKind::IndexLValue;
  }
};

//===----------------------------------------------------------------------===//
// Instructions representing terminators
//===----------------------------------------------------------------------===//

/// This class defines a "terminating instruction" for a BasicBlock.
class TermInst : public Instruction {
protected:
  TermInst(ValueKind K, CFGLocation Loc, Type Ty) : Instruction(K, Loc, Ty) {}
public:

  typedef llvm::ArrayRef<CFGSuccessor> SuccessorListTy;

  /// The successor basic blocks of this terminator.
  SuccessorListTy getSuccessors();

  /// The successor basic blocks of this terminator.
  const SuccessorListTy getSuccessors() const {
    return const_cast<TermInst*>(this)->getSuccessors();
  }

  static bool classof(const Value *I) {
    return I->getKind() >= ValueKind::First_TermInst &&
           I->getKind() <= ValueKind::Last_TermInst;
  }
};

/// UnreachableInst - Position in the code which would be undefined to reach.
/// These are always implicitly generated, e.g. when falling off the end of a
/// function or after a no-return function call.
class UnreachableInst : public TermInst {
public:
  UnreachableInst(CFG &C);
  
  SuccessorListTy getSuccessors() {
    // No Successors.
    return SuccessorListTy();
  }

  static bool classof(const Value *I) {
    return I->getKind() == ValueKind::Unreachable;
  }
};

/// ReturnInst - Representation of a ReturnStmt.
class ReturnInst : public TermInst {
  /// The value to be returned.  This is never null.
  CFGValue ReturnValue;
  
public:
  /// Constructs a ReturnInst representing an \b explicit return.
  ///
  /// \param returnStmt The backing return statement in the AST.
  ///
  /// \param returnValue The value to be returned.
  ///
  ReturnInst(ReturnStmt *S, CFGValue ReturnValue);

  CFGValue getReturnValue() const { return ReturnValue; }

  SuccessorListTy getSuccessors() {
    // No Successors.
    return SuccessorListTy();
  }

  static bool classof(const Value *I) {
    return I->getKind() == ValueKind::Return;
  }
};

/// BranchInst - An unconditional branch.
class BranchInst : public TermInst {
  llvm::ArrayRef<CFGValue> Arguments;
  CFGSuccessor DestBB;
public:
  typedef ArrayRef<CFGValue> ArgsTy;
  
  /// Construct an BranchInst that will branches to the specified block.
  BranchInst(BasicBlock *DestBB, CFG &C);
  
  /// The jump target for the branch.
  BasicBlock *getDestBB() const { return DestBB; }

#if 0
  /// The temporary arguments to the target blocks.
  ArgsTy blockArgs() { return Arguments; }
  const ArgsTy blockArgs() const { return Arguments; }
#endif
  
  SuccessorListTy getSuccessors() {
    return DestBB;
  }
  
  static bool classof(const Value *I) {
    return I->getKind() == ValueKind::Branch;
  }
};

class CondBranchInst : public TermInst {
  /// The condition value used for the branch.
  CFGValue Condition;

  CFGSuccessor DestBBs[2];
public:

  CondBranchInst(Stmt *TheStmt, CFGValue Condition,
                 BasicBlock *TrueBB, BasicBlock *FalseBB);

  CFGValue getCondition() const { return Condition; }

  SuccessorListTy getSuccessors() {
    return DestBBs;
  }
  
  BasicBlock *getTrueBB() { return DestBBs[0]; }
  const BasicBlock *getTrueBB() const { return DestBBs[0]; }
  BasicBlock *getFalseBB() { return DestBBs[1]; }
  const BasicBlock *getFalseBB() const { return DestBBs[1]; }
  
  void setTrueBB(BasicBlock *BB) { DestBBs[0] = BB; }
  void setFalseBB(BasicBlock *BB) { DestBBs[1] = BB; }
  
  static bool classof(const Value *I) {
    return I->getKind() == ValueKind::CondBranch;
  }
};

} // end swift namespace

//===----------------------------------------------------------------------===//
// ilist_traits for Instruction
//===----------------------------------------------------------------------===//

namespace llvm {

template <>
struct ilist_traits<::swift::Instruction> :
  public ilist_default_traits<::swift::Instruction> {
  typedef ::swift::Instruction Instruction;

private:
  mutable ilist_half_node<Instruction> Sentinel;

  swift::BasicBlock *getContainingBlock();

public:
  Instruction *createSentinel() const {
    return static_cast<Instruction*>(&Sentinel);
  }
  void destroySentinel(Instruction *) const {}

  Instruction *provideInitialHead() const { return createSentinel(); }
  Instruction *ensureHead(Instruction*) const { return createSentinel(); }
  static void noteHead(Instruction*, Instruction*) {}
  static void deleteNode(Instruction *V) {}

  void addNodeToList(Instruction *I);
  void removeNodeFromList(Instruction *I);
  void transferNodesFromList(ilist_traits<Instruction> &L2,
                             ilist_iterator<Instruction> first,
                             ilist_iterator<Instruction> last);

private:
  void createNode(const Instruction &);
};

} // end llvm namespace

#endif
