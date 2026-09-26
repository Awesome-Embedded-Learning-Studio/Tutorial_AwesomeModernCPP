---
translation:
  source: documents/vol9-open-source-project-learn/chrome/02_weak_ptr/index.md
  source_hash: bc1e55d95ab8468f752f42dbf229daaa85e63c6e74ffa4166707977880687674
  translated_at: '2026-09-26T02:19:28+00:00'
  engine: anthropic
  token_count: 800
---
# WeakPtr: weak-pointer design lessons from Chromium

This directory implements a Chromium-style `WeakPtr` weak-pointer component and thoroughly unpacks a piece of modern C++ design: a pointer that never touches ownership, yet lets you safely observe whether the object is still alive. It is the sister series to the [OnceCallback series](../01_once_callback/) — the industrial-grade answer to the hand-rolled cancellation token from 01-4 is `WeakPtr`.

## Complete tutorial (full/)

Aimed at readers starting from zero: it opens with weak-reference concepts and prerequisite knowledge, then gradually leads up to a complete component implementation.

Prerequisites (7 articles):

- [WeakPtr prerequisite (0): weak references and the lifetime puzzle](./full/pre-00-weak-ptr-weak-reference-and-lifetime.md)
- [WeakPtr prerequisite (I): intrusive reference counting and scoped_refptr](./full/pre-01-weak-ptr-intrusive-refcount-and-scoped-refptr.md)
- [WeakPtr prerequisite (II): std::atomic and memory_order](./full/pre-02-weak-ptr-atomic-and-memory-order.md)
- [WeakPtr prerequisite (III): sequences, SEQUENCE_CHECKER, and DCHECK/CHECK](./full/pre-03-weak-ptr-sequence-checker-dcheck-check.md)
- [WeakPtr prerequisite (IV): applying concepts and requires](./full/pre-04-weak-ptr-concepts-and-requires.md)
- [WeakPtr prerequisite (V): template friend and uintptr_t type erasure](./full/pre-05-weak-ptr-template-friend-and-uintptr-t.md)
- [WeakPtr prerequisite (VI): TRIVIAL_ABI and trivial relocatability](./full/pre-06-weak-ptr-trivial-abi.md)

Hands-on practice (6 articles):

- [WeakPtr hands-on (I): motivation and API design](./full/02-1-weak-ptr-motivation-and-api-design.md)
- [WeakPtr hands-on (II): the core skeleton and control block](./full/02-2-weak-ptr-core-skeleton-and-control-block.md)
- [WeakPtr hands-on (III): WeakPtrFactory and the last-member idiom](./full/02-3-weak-ptr-factory-and-last-member.md)
- [WeakPtr hands-on (IV): sequence affinity and lazy binding](./full/02-4-weak-ptr-sequence-affinity-and-lazy-binding.md)
- [WeakPtr hands-on (V): integrating with callbacks to close the OnceCallback loop](./full/02-5-weak-ptr-bind-integration.md)
- [WeakPtr hands-on (VI): tests and performance comparison](./full/02-6-weak-ptr-testing-and-perf.md)

## Advanced design guide (hands_on/)

Aimed at readers who already have experience with C++ templates and concurrency: a fast-paced walkthrough of the design motivation, the implementation strategy, and the test verification:

- [weak_ptr Design Guide (I): motivation, API, and the control block](./hands_on/01-weak-ptr-design.md)
- [weak_ptr Design Guide (II): step-by-step implementation](./hands_on/02-weak-ptr-implementation.md)
- [weak_ptr Design Guide (III): test strategy and performance comparison](./hands_on/03-weak-ptr-testing.md)
