# WeakPtr: weak-pointer design, learned from Chromium

This directory implements a Chromium-style `WeakPtr` component and works through the modern C++ design of observing whether an object is still alive, without taking ownership. It sits alongside the [OnceCallback series](../01_once_callback/) — the industrial-strength answer to the hand-rolled cancellation token from 01-4 is `WeakPtr`.

## Full tutorial (full/)

For readers starting fresh, from weak-reference concepts and prerequisites through to a complete implementation.

Prerequisites (7 chapters):

- [weak references and the lifetime puzzle](./full/pre-00-weak-ptr-weak-reference-and-lifetime.md)
- [intrusive refcounting and scoped_refptr](./full/pre-01-weak-ptr-intrusive-refcount-and-scoped-refptr.md)
- [std::atomic and memory_order](./full/pre-02-weak-ptr-atomic-and-memory-order.md)
- [sequences, SEQUENCE_CHECKER, and DCHECK/CHECK](./full/pre-03-weak-ptr-sequence-checker-dcheck-check.md)
- [concepts and requires, advanced](./full/pre-04-weak-ptr-concepts-and-requires.md)
- [template friends and uintptr_t type erasure](./full/pre-05-weak-ptr-template-friend-and-uintptr-t.md)
- [TRIVIAL_ABI and trivial relocatability](./full/pre-06-weak-ptr-trivial-abi.md)

Hands-on (6 chapters):

- [motivation and API design](./full/02-1-weak-ptr-motivation-and-api-design.md)
- [core skeleton and the control block](./full/02-2-weak-ptr-core-skeleton-and-control-block.md)
- [WeakPtrFactory and the last-member idiom](./full/02-3-weak-ptr-factory-and-last-member.md)
- [sequence affinity and lazy binding](./full/02-4-weak-ptr-sequence-affinity-and-lazy-binding.md)
- [callback integration — closing OnceCallback's loop](./full/02-5-weak-ptr-bind-integration.md)
- [testing and performance](./full/02-6-weak-ptr-testing-and-perf.md)

## Hands-on design guide (hands_on/)

For readers comfortable with C++ templates and concurrency, a quick walkthrough of the design motivation, implementation, and testing:

- [motivation, API, and the control block](./hands_on/01-weak-ptr-design.md)
- [step-by-step implementation](./hands_on/02-weak-ptr-implementation.md)
- [test strategy and performance](./hands_on/03-weak-ptr-testing.md)
