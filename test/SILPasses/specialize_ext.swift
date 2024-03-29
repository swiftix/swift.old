// RUN: %target-swift-frontend -disable-func-sig-opts -O -emit-sil -primary-file %s | FileCheck %s

struct XXX<T> {
  init(t : T) {m_t = t}
  mutating
  func foo(t : T) -> Int {m_t = t; return 4}
  var m_t : T
}

extension XXX {
  @inline(never)
  mutating
  func bar(x : T) { self.m_t = x}
}

public func exp1() {
  var J = XXX<Int>(t: 4)
  J.bar(3)
}
//Make sure that we are able to specialize the extension 'bar'
//CHECK: sil shared [noinline] @_TTSg5Si___TFV14specialize_ext3XXX3bar
