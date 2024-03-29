// RUN: rm -rf %t && mkdir -p %t
// RUN: cp -R %S/Inputs/FakeUnavailableObjCFramework.framework %t
// RUN: %target-clang -dynamiclib %S/Inputs/FakeUnavailableObjCFramework.m -fmodules -F %t -framework Foundation -o %t/FakeUnavailableObjCFramework.framework/FakeUnavailableObjCFramework

// RUN: %target-build-swift -F %t %s -o %t/UseWeaklinkedUnavailableObjCFramework
// RUN: %target-build-swift -O -F %t %s -o %t/UseWeaklinkedUnavailableObjCFramework.opt

// These tests emulate deploying back to an older OS where newer APIs are not
// available by linking to an Objective-C framework where APIs have been
// annotated to only be available in the far future (version 1066.0 of all
// platforms) and then moving the famework aside so that it can't be found
// at run time.
// RUN: mv %t/FakeUnavailableObjCFramework.framework %t/FakeUnavailableObjCFramework-MovedAside.framework

// RUN: %target-run %t/UseWeaklinkedUnavailableObjCFramework | FileCheck %s
// RUN: %target-run %t/UseWeaklinkedUnavailableObjCFramework.opt | FileCheck %s

// REQUIRES: objc_interop
// REQUIRES: executable_test
// REQUIRES: enable_target_appletvos

import StdlibUnittest

import FakeUnavailableObjCFramework
import Foundation

// CHECK: Running
print("Running...")

func useUnavailableObjCGlobal() {
  if #available(OSX 1066.0, iOS 1066.0, watchOS 1066.0, tvOS 1066.0, *) {
    let g = UnavailableObjCGlobalVariable
    _blackHole(g)
  }
}

useUnavailableObjCGlobal()

@objc
class ClassConformingToUnavailableObjCProtocol : NSObject, UnavailableObjCProtocol {
  func someMethod() {
    print("Executed ClassConformingToUnavailableObjCProtocol.someMethod()")
  }
}

func useClassConformingToUnavailableObjCProtocol() {
  let o = ClassConformingToUnavailableObjCProtocol()
  o.someMethod()

  if #available(OSX 1066.0, iOS 1066.0, watchOS 1066.0, tvOS 1066.0, *) {
    let oAsUP: UnavailableObjCProtocol = o as UnavailableObjCProtocol
    oAsUP.someMethod()
  }
}

// CHECK-NEXT: Executed ClassConformingToUnavailableObjCProtocol.someMethod()
useClassConformingToUnavailableObjCProtocol()

@objc
class ClassThatWillBeExtendedToConformToUnavailableObjCProtocol : NSObject {
}

extension ClassThatWillBeExtendedToConformToUnavailableObjCProtocol : UnavailableObjCProtocol {
  func someMethod() {
    print("Executed ClassThatWillBeExtendedToConformToUnavailableObjCProtocol.someMethod()")
  }
}

func useClassThatWillBeExtendedToConformToUnavailableObjCProtocol() {
  let o = ClassThatWillBeExtendedToConformToUnavailableObjCProtocol()
  o.someMethod()

  if #available(OSX 1066.0, iOS 1066.0, watchOS 1066.0, tvOS 1066.0, *) {
    let oAsUP: UnavailableObjCProtocol = o as UnavailableObjCProtocol
    oAsUP.someMethod()
  }
}

// CHECK-NEXT: Executed ClassThatWillBeExtendedToConformToUnavailableObjCProtocol.someMethod()
useClassThatWillBeExtendedToConformToUnavailableObjCProtocol()

// We need to gracefully handle ObjC protocols missing availability annotations
// because it is quite common in frameworks. (Historically, for Objective-C,
// missing availability annotations on protocols has not been problematic
// because Objective-C protocol metadata is compiled into any code that
// references it -- it is not weakly linked.)

@objc
class ClassConformingToUnannotatedUnavailableObjCProtocol : NSObject, UnannotatedUnavailableObjCProtocol {
  func someMethod() {
    print("Executed ClassConformingToUnannotatedUnavailableObjCProtocol.someMethod()")
  }
}

func useClassConformingToUnannotatedUnavailableObjCProtocol() {
  let o = ClassConformingToUnannotatedUnavailableObjCProtocol()
  o.someMethod()

  let oAsUP: UnannotatedUnavailableObjCProtocol = (o as AnyObject) as! UnannotatedUnavailableObjCProtocol
  oAsUP.someMethod()
}

// CHECK-NEXT: Executed ClassConformingToUnannotatedUnavailableObjCProtocol.someMethod()
// CHECK-NEXT: Executed ClassConformingToUnannotatedUnavailableObjCProtocol.someMethod()
useClassConformingToUnannotatedUnavailableObjCProtocol()

func printClassMetadataViaGeneric<T>() -> T {
  print("\(T.self)")
  fatalError("This should never be called")
}

func useUnavailableObjCClass() {
  if #available(OSX 1066.0, iOS 1066.0, watchOS 1066.0, tvOS 1066.0, *) {
    let o = UnavailableObjCClass()
    o.someMethod()
  }

  for var i = 0; i < getInt(5); ++i {
    if #available(OSX 1066.0, iOS 1066.0, watchOS 1066.0, tvOS 1066.0, *) {
      let o: UnavailableObjCClass = printClassMetadataViaGeneric()
      _blackHole(o)
    }
  }

  class SomeClass { }
  let someObject: AnyObject = _opaqueIdentity(SomeClass() as AnyObject)

  for var i = 0; i < getInt(5); ++i {
    if #available(OSX 1066.0, iOS 1066.0, watchOS 1066.0, tvOS 1066.0, *) {
      let isUnavailable = someObject is UnavailableObjCClass
      _blackHole(isUnavailable)
    }
  }

  for var i = 0; i < getInt(5); ++i {
    if #available(OSX 1066.0, iOS 1066.0, watchOS 1066.0, tvOS 1066.0, *) {
      let asUnavailable = someObject as? UnavailableObjCClass
      _blackHole(asUnavailable)
    }
  }
}

useUnavailableObjCClass()

// Allow extending a weakly-linked class to conform to a protocol.
protocol SomeSwiftProtocol { }
@available(OSX 1066.0, iOS 1066.0, watchOS 1066.0, tvOS 1066.0, *)
extension UnavailableObjCClass : SomeSwiftProtocol {
}

// CHECK-NEXT: Done
print("Done")
