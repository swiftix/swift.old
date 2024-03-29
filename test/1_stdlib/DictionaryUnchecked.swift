// RUN: mkdir -p %t
// RUN: %target-build-swift %s -o %t/a.out -Ounchecked
//
// RUN: %target-run %t/a.out
// REQUIRES: executable_test

import StdlibUnittest

// Also import modules which are used by StdlibUnittest internally. This is
// needed to link all required libraries in case we serialize StdlibUnittest.
import SwiftPrivate
#if _runtime(_ObjC)
import ObjectiveC
#endif

var DictionaryUnchecked = TestSuite("DictionaryUnchecked")

DictionaryUnchecked.test("noCseOnInit") {

  @inline(never)
  func createDict() -> Dictionary<Int, Bool> {
    // CSE should not be able to combine both Dictionary.init() calls.
    // This did happen and resulted in a crash because Dictionary.init()
    // was defined with @effects(readnone).
    // But this was wrong because it actually reads the array buffer (from
    // the literal).
    var Dict: Dictionary<Int, Bool> = [:]
    Dict = [:]
    Dict[0] = true
	return Dict
  }

  let Dict = createDict()
  expectTrue(Dict[0]!)
}

runAllTests()

