---
title: "Linked Lists: Never Move House — the Cost Is Asking the Way"
description: "Part 2 of the data structures primer. Last time, dynamic arrays made everyone shift seats for a front insert; this time we tear out the premise of 'contiguous': data goes into nodes strung together by pointers, and insertion/erasure only rewires pointers without disturbing anyone — front-inserting 100,000 elements measured vector at about 317 ms and list at about 2.7 ms. The costs arrive right on cue: there is no subscript, and jumping to element #900,000 leaves list more than four orders of magnitude slower; sequential traversal pays the cache price because nodes are scattered across the heap, measured about 15x slower; and every node carries two extra pointers on its back. The address experiments supply direct evidence: after a node is erased, the remaining addresses do not budge, and node positions do not change by one digit across a splice — never moving house is real. All output comes from real runs on this machine, not retelling."
chapter: 7
order: 2
tags:
  - host
  - cpp-modern
  - beginner
  - 容器
  - 基础
  - 入门
difficulty: beginner
platform: host
reading_time_minutes: 15
prerequisites:
  - "Dynamic Arrays: A Block of Memory That Moves House"
  - "Memory Layout"
  - "Pointer Basics"
related:
  - "deque, list, and forward_list: Three Alternatives to vector"
  - "Deep Dive into std::vector: Three Pointers, Reallocation, and Iterator Invalidation"
cpp_standard: [11]
translation:
  source: documents/vol3-standard-library/containers/primer/02-linked-list.md
  source_hash: 6dc7f28fdba8906651bd38dec00fbaee461ec01f952892f0df0403375636e73f
  translated_at: '2026-09-25T08:48:33+00:00'
  engine: anthropic
  token_count: 3200
---

# Linked Lists: Never Move House — the Cost Is Asking the Way

Last time we left dynamic arrays with a hurdle: insert a 99 at the front, and the 8 old elements behind all shift right one slot — the closer to the front you insert, the more everyone moves. The root cause comes down to one word: **contiguous**. The slots must line up in a row for `a[i]` to have an address formula to compute and for the cache to have locality to exploit; yet that very "lining up in a row" is also what saddles middle insertion and deletion with moving duty: slot 0 has to make room for the newcomer, and everyone behind is in the way.

So what if we tear out "contiguous"? The data no longer has to crowd onto one street: everyone lives in their own house, and inside each house there is a slip of paper that says "where the next house is". With that demolition, the moving duty of insertion and deletion disappears — the price is that finding an element no longer has a shortcut; all you can do is follow the slips and ask door to door. In this article we build the linked list up from nodes, and lay out both its bargains and its costs with data from real runs.

## Breaking the Slots into Nodes, Strung Together with Pointers

The linked list's building block is the **node**: one piece of data plus one pointer pointing onward, living on the heap. You can think of `next` as a slip of paper that says "where the next house is" — we will lean on this metaphor for the whole article.

```cpp
struct Node {
    int   val;    // the data being stored
    Node* next;   // points to the next node; the list tail stores nullptr here
};
```

Let's string three nodes together by hand, to get a feel for the structure:

```cpp
Node* head = nullptr;             // entry pointer: empty list

Node* c = new Node{30, nullptr};  // list tail: next stores nullptr
Node* b = new Node{20, c};        // middle node: next points to c
Node* a = new Node{10, b};        // head node: next points to b
head = a;                         // the entry points at the head node
```

![The head pointer points to the first node; each node carries val and next, and the last node points to nullptr](./02-node-chain.drawio)

Compare with the previous article: `head` plays the role of `IntVec::data` — the handle we hold in our hand. The difference is that the 8 slots `data` points to are **one whole contiguous block of memory**, whereas the first node `head` points to has no geographic relationship whatsoever with its next house: the three nodes come from three independent `new`s, and they live wherever the allocator puts them. To reach the 30 in the third node, there is no formula — only one road: walk from `head` to the first house, read its slip, walk to the second house, read the slip again, and only then do you arrive at the third.

The end of the chain is `nullptr`: the last house's slip reads "no more", and that is our termination condition for traversal. The integrity of the whole chain hangs entirely on the slips: lose one, and the entire segment after it becomes an unrecoverable orphan (a leak); write one wrong, and you walk into a house you never meant to visit (undefined behavior).

## Insertion and Deletion: You Edit the Slips, Not the Houses

Now let's come back to that hurdle. Inserting a 99 at the front of the list — who has to move?

```cpp
void push_front(Node*& head, int x) {
    Node* n = new Node{x, head};  // the new node's next points to the old head node
    head = n;                     // head now points to the new node
}
```

Two lines, done. The new node writes its slip before moving in (it points at the old first house), and then the entrance is redirected. And the old nodes? We go ask them one by one, and every answer is the same: don't know, don't care, not moving — not a single address on their slips changed. Deletion works the same way: to remove a house, only its **previous house** has to rewrite its slip, from "pointing at it" to "pointing at its next house", and then the node is `delete`d. From "the entire back half shifts seats" to "rewrite two slips" — that is the first benefit that tearing out contiguity buys.

We have put this sequence into the online compiler, so you can run it and modify it yourself:

<OnlineCompilerDemo
  title="Hand-Stringing a List and Inserting at the Front"
  source-path="code/examples/vol3/primer_02_node_chain.cpp"
  description="Three nodes strung together by hand, push_front inserts a 99, traversal prints them, and each node is freed at the end. You can also add your own middle-insertion code in the editor to get a feel for the pointer-only insert."
  allow-run
/>

We race `std::list` (the standard library's doubly linked list — more on it in the wrap-up next article) against `std::vector`: each front-inserts 100,000 elements, three timed rounds (MSVC x64 `/O2`):

```text
run 1: front-insert 100000 elems | vector =   317366 us | list =   2783 us
run 2: front-insert 100000 elems | vector =   316133 us | list =   2785 us
run 3: front-insert 100000 elems | vector =   317236 us | list =   2546 us
```

Look at the numbers: vector about 317 ms, list about 2.7 ms — a hundredfold either way. That hundredfold is no dark magic; the previous article already took it apart: every front insert into vector has to shift away all existing elements, roughly 5 billion element moves in total for 100,000 elements; every front insert into list is a constant number of actions — allocate a node, write two slips. **O(n) versus O(1): once the scale grows, it is orders of magnitude.**

But let's say it upfront: what we compared above was front insertion — each structure's **expensive item pitted against the other's cheap one**. To judge the overall winner, we also have to lay out list's costs, and the next three sections tally them one by one.

## No Subscript: Finding an Element Is Down to Asking the Way

Why is vector's `v[900000]` a one-step arrival? Because the slots are contiguous, the address has a formula: base address plus 900000 times the slot width — one computation, pure arithmetic. A linked list's nodes are scattered across the heap; no formula whatsoever can compute where the 900000th node lives — our only option is to walk 900000 steps from the head. So `std::list` simply **has no `operator[]`**: writing `L[5]` does not even compile. This is not the standard library being lazy — a linked list genuinely cannot do O(1) subscripting; even if the interface were offered, every use would still be a walk from the head.

We ran the comparison for real: access element #900,000 100 times each (`std::next` is the standard spelling of "walk n steps along the chain"):

```text
jump to elem #900000 x 100 tries | vector = 2 us | list = 164761 us
```

vector takes about 20 nanoseconds per access, list about 1.6 milliseconds — a gap of more than four orders of magnitude. Worse, this cost **cannot be amortized**: vector's moving is expensive only occasionally, and it amortizes down to a constant; the linked list's way-asking is expensive every single time — i steps is i steps, and the 10th ask costs exactly as much real work as the 1,000,000th.

So in the linked list's world, the very notion of "position" changes flavor. In vector, the subscript is the position — hold it and you jump. In a linked list, the only positions you can jump to in one step are the two ends (`begin()` and `end()` — plus `rbegin()`, the slot before `end()`, which counts too, since a doubly linked list has an entrance at the tail); every position in between, we have to walk to. This is also why the previous section could say deletion only needs the predecessor to rewrite a slip at O(1) cost, yet carried a precondition everywhere — **the precondition is that you have already walked there and are holding an iterator in your hand**. Position itself must be bought with O(n).

⚠️ The sentence "linked-list insert/erase is O(1)" omits its subject: it is "insert/erase **at a known position**" that is O(1); finding the position is a separate O(n). Pitting that against "vector subscript access is O(1)" in the ring compares two things that are not the same thing at all.

## The Cache Hurdle: The Price of Being Scattered Across the Heap

The second cost is more hidden: it lurks in the hardware, and no amount of staring at the code will show it.

By rights, sequential traversal is a linked list's most presentable job: head to tail, not one detoured step, complexity O(n) the same as vector. But let's actually run it: both containers hold 1,000,000 elements, summed from head to tail, timed over 20 rounds:

```text
walk 1000000 elems x 20 rounds | vector = 2462 us | list = 37271 us | sink = 19999980000000
```

Same 1,000,000 elements, same single pass, and list is about 15x slower. Same complexity, but the constant differs by an order of magnitude — and the difference is in the **cache**.

A CPU does not fetch memory byte by byte; it moves whole **cache lines** (usually 64 bytes) at a time. Vector's 1,000,000 `int`s are packed into about 4 MB of contiguous memory: one cache miss pulls 64 bytes into the cache, all 16 `int`s inside are there, and the next 15 accesses are all cache hits; moreover, the access pattern is a strict "addresses increasing", the hardware prefetcher recognizes the rhythm and hauls later lines in ahead of time, so the data is already in place by the time the CPU actually needs it. Traversing a vector, we barely wait at all.

And the linked list's nodes? The address experiment lets us see directly how scattered their homes are. Look at the real output (a `std::list` holding 8 elements, printing each element's address):

```text
list of 8, element addresses:
  [ 0] 0000019B99206B80
  [ 1] 0000019B99206BC0
  [ 2] 0000019B99206C00
  [ 3] 0000019B99206C20
  [ 4] 0000019B99206C40
  [ 5] 0000019B99206C60
  [ 6] 0000019B991FEBB0
  [ 7] 0000019B991FE670
```

Read this output: the first six nodes happened to land in the same neighborhood (that is already good luck — on consecutive `push_back`s the allocator habitually piles same-sized blocks in one place), while the seventh and eighth were assigned to another street more than 30 KB away. And this is only 8 nodes; on a million-node chain, the physical addresses are all over the map. Every step is a jump to whatever address `next` points to: a node is 24 bytes, not enough to fill a cache line, so one miss serves exactly one element; and the jumped-to addresses follow no pattern whatsoever, leaving the prefetcher completely out of a job. **One cache miss per element** — that is how the 15x accumulates.

![Cache view: vector's contiguous slots let one cache line serve several elements along the way; linked-list nodes are scattered, one jump per step](./02-cache-locality.drawio)

This lesson deserves its own line in our notebook: **same complexity, different physical layout — performance can differ by an order of magnitude**. From now on, whenever you read any "linked list or array" discussion, locality is the hidden variable you cannot get around; later in this tutorial, when we talk about container choice and data-structure design, we will keep coming back to this lesson.

## The Extra Pointers Every Node Carries

The third cost is the most blunt: memory. Nodes must carry slips, and the slips themselves take up space. Let's run it for real (x64):

```text
sizes: int=4 ptr=8 DNode=24 SNode=16 std::list<int>=16 std::forward_list<int>=8
```

Let's do the arithmetic with `DNode`: it is the doubly-linked-list node (the data plus two pointers, `next` and `prev`) — a 4-byte `int`, 16 bytes of pointers, plus 4 bytes of alignment padding, so 24 bytes per node. **Storing 4 bytes of data in a 24-byte house**: a 6x overhead. The singly linked list's `SNode` drops the `prev`, comes in at 16 bytes — still 4x. This is not `std::list` being clumsy; it is simply how this structure has to be built: the standard library's `std::list` nodes have exactly the same "one value plus two pointers" figure.

The good news is that this ratio thins out as elements grow: storing `int`, the pointers take two-thirds of the space; store a 1 KB struct and the pointers are down to one and a half percent. So "linked lists have big memory overhead" needs a nuanced reading: **dense scenes of small elements are the disaster zone**, and precisely those scenes also eat the cache loss from the previous section — the two weak spots very often show up together.

The last two numbers in the output also deserve a second look: the `std::list<int>` object itself is only 16 bytes — one entry pointer at each end of the doubly linked chain, quite slim; `std::forward_list<int>` is a mere 8 bytes, a single pointer. But `forward_list` saves more than just nodes: it does not even provide `size()`. Why? Because for a singly linked list to count its own elements, the only way is to walk the whole chain, O(n) — so the standard simply withholds the interface, forcing users who do not need the count to save the per-node counting overhead. Savings pushed this far change even the API.

## What Never Moving House Means: Addresses, Iterators, and splice

With all three costs laid out, let's turn back to the linked list's most valuable property: **a node lives at the same address from birth to death**. No moving means the pointers, references, and iterators pointing at a node are not invalidated by other insertions or deletions; contrast the previous article's vector, where "moving house means every old address is voided" — that is a difference in kind. Claims need proof; here are real runs.

Experiment one: we erase a node from the middle of the list and look at the remaining nodes' addresses:

```text
after erasing the node holding 4:
  [ 0] 0000019B99206B80
  [ 1] 0000019B99206BC0
  [ 2] 0000019B99206C00
  [ 3] 0000019B99206C20
  [ 5] 0000019B99206C60
  [ 6] 0000019B991FEBB0
  [ 7] 0000019B991FE670
```

Check against the pre-erase listing: the 7 remaining nodes have not moved a single digit of their addresses. The deleted house has been returned (`delete`), the slips of the houses before and after it are rewritten, and everyone else lives on as before. And vector in the same experiment, after erasing `v[4]`? Element 5 moves into 4's old address, 6 into 5's, 7 into 6's — the whole back half shifts forward one slot, a physical move in the flesh.

You can run this experiment online too:

<OnlineCompilerDemo
  title="Erasing a Node: Every Other Address Stays Put"
  source-path="code/examples/vol3/primer_02_address_stability.cpp"
  description="Erase the node holding 4; the addresses of the other 7 nodes are exactly what they were before the erase. If you feel like it, swap std::list for std::vector and do the same thing — the addresses all shift forward, and the previous article's scene replays."
  allow-run
/>

The second experiment has more flavor: it is the linked list's signature dish, `splice` — cutting a whole segment out of the middle of one chain and grafting it onto another. We cut 11, 12, 13 out of the middle of chain `b` and graft them onto the tail of chain `a`:

```text
after splice: a = 0 1 2 11 12 13 | b = 10 14
node addresses now: 11@0000019B991FE5F0 12@0000019B991FE5B0 13@0000019B991FE990 (unchanged)
```

Line up the three node addresses once more: new owner, not one digit changed — **what moved is ownership, not the houses**. The operation rewrote only the few pointers at the boundaries (a's tail links to 11, and 13 links to the 14 left behind in `b`), touched not a single element, and finished in O(1). Hand the same job to vector, and it has to copy the three elements over and then shift `b`'s tail forward to fill the hole — O(n) on both ends. Any scene that "hands a stretch of data between containers" (merging task queues, handing off buffers, relocating timer-wheel slices) is where the linked list's "change ownership only" ability is a proprietary edge.

![splice: the nodes do not move; four boundary pointers are rewired, and ownership moves from b to a](./02-splice.drawio)

⚠️ The only way a linked-list iterator is invalidated is **the node it points to being erased**. When erasing, we catch the returned next position with `it = L.erase(it)` and keep walking — the standard posture for erase-while-traversing a list; dereferencing an iterator to an already-erased node is the same kind of accident as last article's dangling pointer.

## When It Is the Linked List's Turn

Three costs (O(n) way-asking, the cache disadvantage, pointer overhead) against one strength (O(1) insert/erase at known positions, plus stable addresses) — the shape of the choice is actually clear: **where we already hold the positions in hand and will insert/erase at those positions frequently, the linked list wins**: maintaining a crowd of long-lived observers/callbacks, an LRU cache's eviction chain, a timer queue — all of that shape; conversely, **where subscript access and whole-range scanning dominate, vector wins**, and it wins harder than the complexity table suggests (that 15x constant gap).

There is also an often-overlooked middle camp: for elements that are large and expensive to move, the contrast between the linked list's "never moves" and vector's "relocate everything on growth" gets amplified further — moving an 8-byte `int` is an instant inside a memmove, while moving a million non-trivial objects is a full-fledged storm of destructors plus constructors (that is where the `move_if_noexcept` subtlety from the previous article lives). The formal usage and pitfalls of `std::list` and `std::forward_list` (why `merge` and `sort` are linked-list-specific versions, the triangle with `deque`) unfold in this volume's [`deque, list, and forward_list`](../05-deque-list-forward-list.md); and if you want to hand-roll one yourself, vol8's mini STL series is queuing up for us.

The primer's next stop is the hash table: a structure that kneads these two articles together — **an array as the index, a linked list as the cargo hold** — borrowing half of random access's speed and half of insertion/deletion's agility, each from one side. See you in the next article.

## References

- [cppreference: std::list](https://en.cppreference.com/w/cpp/container/list) — complexity guarantees, iterator invalidation rules, and the semantics of `splice`/`merge`
- [cppreference: std::forward_list](https://en.cppreference.com/w/cpp/container/forward_list) — the singly linked list, and the design note on "why there is no size()"
- In this volume: [`deque, list, and forward_list`](../05-deque-list-forward-list.md) — the deep dive on `std::list` and choosing between containers
- In this series: [the previous article — Dynamic Arrays, a Block of Memory That Moves House](./01-dynamic-array.md) — the cost bill of contiguous storage, this article's control group
