//===-- Devirtualize.h - Helper for devirtualizing apply --------*- C++ -*-===//
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
// This contains helper functions that perform the work of devirtualizing a
// given apply when possible.
//
//===----------------------------------------------------------------------===//

#ifndef SWIFT_SIL_DEVIRTUALIZE_H
#define SWIFT_SIL_DEVIRTUALIZE_H

#include "swift/AST/Decl.h"
#include "swift/AST/Types.h"
#include "swift/SIL/SILDeclRef.h"
#include "swift/SIL/SILFunction.h"
#include "swift/SIL/SILInstruction.h"
#include "swift/SIL/SILModule.h"
#include "swift/SIL/SILType.h"
#include "swift/SIL/SILValue.h"
#include "swift/SILPasses/Utils/Local.h"
#include "llvm/ADT/ArrayRef.h"

namespace swift {
/// A pair representing results of devirtualization.
///  - The first element is the value representing the result of the
///    devirtualized call.
///  - The second element is the new apply/try_apply instruction.
/// If no devirtualization was possible, the pair:
/// <nullptr, FullApplySite()> is returned.
///
/// Two elements are required, because a result of the new devirtualized
/// apply/try_apply instruction (second element) eventually needs to be
/// casted to produce a properly typed value (first element).
typedef std::pair<ValueBase *, FullApplySite> DevirtualizationResult;

DevirtualizationResult tryDevirtualizeApply(FullApplySite AI);
bool isClassWithUnboundGenericParameters(SILType C, SILModule &M);
bool canDevirtualizeClassMethod(FullApplySite AI, SILType ClassInstanceType);
DevirtualizationResult devirtualizeClassMethod(FullApplySite AI,
                                               SILValue ClassInstance);
DevirtualizationResult tryDevirtualizeClassMethod(FullApplySite AI,
                                                  SILValue ClassInstance);
}

#endif
