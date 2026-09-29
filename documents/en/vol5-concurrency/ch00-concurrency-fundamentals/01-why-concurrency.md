---
title: "Why Concurrency: A Blocked Main Loop"
description: "Start from a main loop frozen by a blocking network call, tell concurrency apart from parallelism, lay down the volume's three principles, and use Amdahl's Law to weigh the ceiling on parallel speedup"
chapter: 0
order: 1
tags:
  - host
  - cpp-modern
  - beginner
  - 基础
  - 入门
difficulty: beginner
platform: host
reading_time_minutes: 12
cpp_standard: [11, 17, 20]
related:
  - "Data Races and ThreadSanitizer, Lesson One"
  - "std::thread Basics"
translation:
  source: documents/vol5-concurrency/ch00-concurrency-fundamentals/01-why-concurrency.md
  source_hash: 4bfd0ae7250e49383acec4b29a974ad9e4b95d1b931a6c82bef6f5116acb57e4
  translated_at: '2026-09-29T14:40:19+00:00'
  engine: anthropic
  token_count: 6300
---

# Why Concurrency: A Blocked Main Loop

Hey, welcome to concurrency. Let me put my cards on the table right away: a project of real scale and seriousness ends up dealing with concurrency sooner or later. These days a desktop CPU with a dozen cores or a server with a hundred is nothing unusual, and if you dump all your code onto a single logical core, you have simply paid for the rest of that silicon and left it sitting there.

But hold on before you rush off to rewrite the whole project as "highly concurrent." Rather than argue about whether concurrency actually speeds anything up, I want to flag one thing first: **concurrency is hard to write, and speed is not the first thing it buys you.** This article contains not one line of thread code. We only want to make three questions clear: when is concurrency genuinely unavoidable, how exactly do concurrency and parallelism differ, and how does this volume plan to take you through the subject.

## The Single-Threaded World, and What Was Good About It

You have probably lived with single-threaded code for a long time, and those were genuinely comfortable days. The order in which functions are called is the order in which they execute; the value you read out of a variable is the value you last wrote into it; everything can be accounted for. When the program goes wrong, you take a debugger and single-step through it, and the answer to which line did it is also deterministic. Reproducing a bug is almost a non-issue in single-threaded code: the same input gives you the same trajectory a hundred times out of a hundred.

> Our terminology card: a **flow of execution**. Throughout this volume we use it as the umbrella term for one sequentially executing path through the code. An OS thread is one kind of flow of execution, a coroutine is another, and even that short stretch inside a signal handler counts. It is fine if you have not heard of the last two; just remember the first sentence for now.

Comfortable days always come to an end. Picture a data acquisition device: it has to read sensors on a millisecond cadence, ship the data it has accumulated out over the network, and refresh a status screen now and then. Written single-threaded, all three jobs get lined up inside one loop:

```text
while (running) {
    read a batch of sensor data;      // fast, microseconds
    push a batch to the network;      // slow, can stall for hundreds of milliseconds
    refresh the status screen;        // fast, but it has to keep to its cadence
}
```

Now we are in trouble. The moment the network push **blocks** (it sits there waiting for the other end to answer, and until that answer arrives it does not move a step), the **entire main loop** freezes in place. The screen freezes along with it, and nobody is minding the sensors. By the time the network call times out and comes back, the accumulated data already has a gap in it, and the screen is still drawing values from several seconds ago.

> Let us set up one more terminology card: **blocking**. A piece of code has started an operation that waits on some external result, and until that result comes back it cannot take a single step of its own; that state is what we call blocking. Waiting on a disk, waiting on the network, waiting on another flow of execution — all of it blocks us.

This kind of trap is not the acquisition device's alone: we have all seen desktop software whose whole interface goes unresponsive while a network request is in flight. Servers are the same story — if one client's slow connection jams the entire service, every request behind it is stuck in line. The root cause, stripped down, is quite plain: sampling, reporting, and refreshing the screen are three jobs that could perfectly well be done independently, but crammed into one loop, the slowest of them blocks all the rest.

## Hardware Changed Direction Too

Over these same years, hardware has been shifting gears as well. In the mid-2000s, rising CPU clock speeds hit the power wall, and vendors moved their effort from "make a single core faster" to "put several more cores on the chip." Hardware has served parallel execution up on a platter; whether you can use it, and whether you use it well, comes down to the concurrency skills on the software side.

Back to the work in front of us: the module has to go faster, the cores have to be fed, the blocking has to be undone. Look all the way around and there is really only one road — split these jobs apart and open up several flows of execution to work on them separately.

Once you split them, the payoffs are immediate, so let us tick through them. The first is **separation of concerns**: sampling, reporting, and screen refresh were always three logically independent jobs, and once they become three flows each minding its own business, the code in every flow gets simpler. When you change the reporting logic, you no longer have to keep the screen's refresh cadence in your head, and the retry and backoff of reporting (try again after a while when it fails, waiting longer after each failure) no longer has to get tangled up with the sensor's sampling rhythm. This holds on a single-core machine too; the structural clarity alone already makes it worthwhile.

The other is **performance**, and it has two sources. One is multi-core parallelism: spreading the computation that can be separated across several cores to be computed at the same time, which is the headline act of the chapters ahead. The other is plainer, and we call it **overlapping waits**: while waiting on a disk or the network the CPU is idle, and concurrency lets us push other work forward while one I/O is in flight, so even a single-core machine can put that idle stretch to use. The acquisition device example actually touches both sources: while the report is blocked, sampling and screen refresh should not be buried along with it, and that is overlapping waits at work; if the data volume ever grows past what one core can handle, then we can talk about spreading the computation out.

Williams opens the same way in the first chapter of *C++ Concurrency in Action*: when he pitches what threads are good for, he leads with handing different responsibilities to different flows of execution, and puts performance after that. That is the order this volume follows as well.

Nothing comes for free. Once two flows of execution are actually running, the questions come as a new batch: who protects the shared data? Who wrote it, who read it, and how do we reckon the ordering? And when something goes wrong, how do we track it down? The answers to those questions are the body of the eight chapters that follow.

## Concurrency and Parallelism: Two Words People Keep Mixing Up

Before we go further, we have to pin down two words that everyone uses interchangeably. If these two stay vague, then every later discussion — "did we actually use the cores," "does a coroutine really speed things up" — stays vague along with them.

In his 2012 talk *Concurrency Is Not Parallelism*, Rob Pike said something that has traveled far and wide. Here is the line verbatim:

> `Concurrency is about dealing with lots of things at once. Parallelism is about doing lots of things at once.`

Put plainly: concurrency is about **dealing with** many things at the same time, parallelism is about **doing** many things at the same time. The same slide deck has an even shorter line: `Concurrency is about structure, parallelism is about execution.` — concurrency cares about structure, parallelism cares about execution. The two sentences differ by a single word, and what differs is where they set their sights.

**Concurrency cares about structure**: break a problem into several parts that can advance in turns, then manage them with mechanisms like threads, coroutines, and event loops (the last two are new faces that only appear in Chapter 6; for now, hearing the names is enough). Concurrency does not demand multiple cores: on a single-core machine, the operating system's time-slice rotation alone can produce the effect of concurrency.

> One more terminology card: **time-slice rotation**. The operating system cuts CPU time into little slices and hands them out to the threads in turn. Macroscopically they look like they advance simultaneously; microscopically, only one of them is running at any given instant.

**Parallelism cares about execution**: several hardware units genuinely doing their own work at the very same instant — multi-core CPUs, multiple processors, and GPUs all count. Without multiple execution units, parallelism is out of the question; some core has to do the work for it.

Let us take a quick look at our own machine:

```bash
nproc                            # logical core count
lscpu | grep -E 'Core|Socket'    # physical cores per CPU and number of sockets
cat /proc/loadavg                # average load over the last 1/5/15 minutes
```

These commands cost you less than ten seconds, yet they let everything we discuss later land on your own machine.

> Sidebar: what `nproc` counts is logical cores. On a machine with hyper-threading enabled, one physical core corresponds to two logical cores; small concurrent tasks can get something out of that, but a compute-saturating parallel task will not necessarily double. How many cores you count does not actually matter; what matters is that you know what it is you counted.

Why is this distinction worth half a section? Because in C++, the tools in our hands — `std::thread`, `std::async`, and coroutines — all express the **concurrency** structure. Whether those tasks end up squeezed onto one core taking turns by time slice, or spread across different cores genuinely running at once, depends on the operating system's scheduling and the hardware's capability, and is not something we directly command. What we are responsible for is the other thing: **however many cores the program ends up using, it has to be correct.** Speedup is a layer of benefit on top of correctness, and that order cannot be reversed.

As for what the standard library has in its toolbox, you will get your hands on each piece in turn later; there is no need to memorize names right now.

## Three Principles, Standing at the Head of the Volume

Around this distinction between structure and execution, the volume has three principles, and every chapter makes its trade-offs by them. You can read them as slogans for now; behind each one stand several chapters of substance, and after you have gone all the way around and look back, they will no longer be slogans.

| Principle | Why, in one line | Where it lands |
| ------------------ | -------------------------- | --------------------------------------------------------------------------------------- |
| `correctness first, performance second` | fast and wrong is meaningless | Starting from the next article on data races and TSan, throughout the volume |
| `locks first, lock-free later` | lock-free raises the bar for correctness even higher | [Chapter 2 · Sharing and Synchronization](../ch02-mutex-condition-sync/) → [Chapter 4 · Lock-Free and Measured Performance](../ch04-concurrent-data-structures/) |
| `synchronization first, tasks later` | the task layer is built on top of synchronization primitives | [Chapter 2](../ch02-mutex-condition-sync/) → [Chapter 5 · From Threads to Tasks](../ch05-future-task-threadpool/), [Chapter 6 · Coroutines](../ch06-async-io-coroutine/) |

The first one is about ranking values: concurrent code that runs blazing fast but goes haywire every so often is far worse than code that is somewhat slower but behaves deterministically. The reason hides in the shape of concurrency bugs — two flows of execution reading and writing the same memory without synchronization is undefined behavior in the C++ standard, and the outcome may be a crash on the spot, or it may look fine for a long time while quietly corrupting data somewhere else, and it is extremely hard to reproduce. As for what "locks" and "tasks" are, you have not met them yet; that is fine. Just record the ordering for now — the names will all be cashed in later.

## The Payoff Has a Ceiling, and the Serial Part Decides It

With the principles laid down, let us do a bit of arithmetic on the payoff. The hardware got eight times bigger; how many times faster does the program run? That is exactly the question Amdahl's Law answers.

Let the fraction of the program that can be parallelized be `f`, leaving the rest, `1 - f`, which can only run serially. With `N` processors, the theoretical speedup is:

`S(N) = 1 / ((1 - f) + f / N)`

You can see this formula's temperament at a glance: no matter how many cores you hand the serial part to, it can only run on its own, and the `1 - f` in the denominator squats there forever. As `N` tends to infinity, `f / N` tends to zero, and the speedup touches its ceiling of `1 / (1 - f)`. Let us plug in a few sets of numbers:

| Scenario | Substitution | Speedup |
| ------------------- | ---------------------- | ------- |
| 4 cores, 90% parallelizable | `1 / (0.1 + 0.9/4)` | 3.08× |
| 8 cores, 90% parallelizable | `1 / (0.1 + 0.9/8)` | 4.71× |
| 16 cores, 75% parallelizable | `1 / (0.25 + 0.75/16)` | 3.37× |
| 1024 cores, 90% parallelizable | `1 / (0.1 + 0.9/1024)` | 9.91× |

The last row is the one that jumps out: a machine with one thousand and twenty-four cores, with ninety percent of the code parallelizable, still buys you less than a tenfold speedup. That serial 10% pins a thousand-core ambition down to a single digit.

> Sidebar: the numbers in this section are pure mathematical derivation and have nothing to do with the measured speedup of any real program. They draw the ceiling; real measurements still carry the weight of scheduling, caches, and synchronization. In engineering work, the proper way to get `f` is to profile (performance profiling: measuring how much time each part of the program takes) the serial version's per-part timings. It is fine if your first estimate is rough — finish a version and come back to fix it; Amdahl's number is a living budget. The discipline of real measurement gets established later, in Chapter 4's perf class.

Some people read Amdahl as an argument that multiple cores are useless, which is a twisted reading. What it really says is that **reworking the serial part matters just as much**: to get an 8× speedup out of 16 cores, maxing out the parallel portion is not enough. Even with ninety percent parallelizable, 16 cores only produce 6.4×, and that remaining tenth still has to be slimmed down.

::: details One layer deeper: Gustafson's Law and where Amdahl came from

Amdahl looks pessimistic because it assumes the problem size stays fixed: more cores, but still the same size of problem. In 1988 Gustafson took a different angle — in the real world, people handed more compute often turn around and solve a bigger problem, letting the work in the parallel part inflate linearly with the core count while the serial part stays where it is. Under that framing the speedup is written `S(N) = α + (1 - α) · N`, where `α` is the serial fraction (it and Amdahl's `f` are two sides of one coin). On the same 16 cores with `α` at 0.1, it gives 14.5×, and all at once the picture turns optimistic.

The two laws do not actually contradict each other; they answer two different questions:

| Question | Law | Framing |
| -------------------------- | --------- | ---------------------------- |
| How much faster can the same batch of work go | Amdahl | problem size fixed, the serial fraction drags its feet |
| How much more work can you get done in the same time | Gustafson | problem size grows with the core count |

One more piece of trivia worth mentioning: Amdahl's original 1967 paper has not a single equation in it, only one figure; the formula we use today was distilled into a common formulation by later authors from his argument. When IEEE SSCS News reprinted the paper in 2007, the editor's note put it bluntly: `it has no equations and only a single figure`.

:::

## When Not to Use Concurrency

After all this talk about why we would use it, we also need to know when not to.

The first category is the single task that is **CPU-bound and cannot be separated**, such as a stretch of pure numerical computation with its dependencies tangled together. Recurrences, loops that solve iteratively, chained transformations — the next step needs the previous step's result, so they are serial by nature. Multiple threads cannot help, and forcing a split only costs you synchronization overhead. The second category is programs that are **already fast enough**, whose latency sits far below the business threshold; here the complexity that concurrency brings is not worth it. The third is settings that **demand strict determinism**: some control systems require every response to land inside a predictable window, and the scheduling jitter that multiple threads introduce may simply be unacceptable — constraints like that are not rare in embedded work.

> Sidebar: hard real-time takes an entirely different road, where correctness means a guaranteed upper bound on time — a different set of rules from this volume's measure-after-the-fact discipline. We will not expand on it in this volume; we only want to remind you not to mix the two yardsticks.

One more category hides somewhat deeper: what you actually need is **asynchronous I/O**, not parallel computation. A network service has to serve thousands upon thousands of connections at once, and if you spawn one thread per connection, the number of threads itself becomes the bottleneck. The right answer for such cases is event-driven design or coroutines, using a small number of threads together with I/O multiplexing (letting one thread watch a whole pile of connections and serve whichever one is ready) to manage a large number of connections. That is the territory of [Chapter 6, Coroutines](../ch06-async-io-coroutine/); we are just planting a signpost here.

### How Fine Should the Split Be

Once you have really decided to go concurrent, there is one more piece of judgment to train: **task granularity**, meaning how large a unit you chop the work into before handing it off. Split it too fine and the overhead eats the bulk of the payoff — creating and destroying threads, context switches, entering and leaving locks: every one of them sends you a bill. If a task's own computation is smaller than these costs, concurrency actually slows the program down. Take an extreme and it becomes easy to see: spawn a thread for each element of an array just to do one addition, and the cost of managing the threads can be hundreds or thousands of times that addition.

Splitting too coarsely has a price too: a pile of cores sit watching one thread work while the rest of the compute idles for nothing. So is there a numeric threshold for granularity that you can memorize? Our honest answer: there is no universally accepted value. We only set a qualitative requirement — **a task's computation should be substantially larger than the cost of creating and scheduling it.** How many microseconds it takes to start an empty thread on your own machine is something [OS Threads and Their Cost at the head of Chapter 4](../ch04-concurrent-data-structures/00-os-threads-and-cost.md) will measure for you on the spot.

> Sidebar: the right-sized granularity is measured, not guessed. On the same machine, a compute-bound task and an I/O-bound task can differ by an order of magnitude in what granularity fits, which is why we weigh the discipline of measurement more heavily than any rule of thumb.

Finally, there is a pair of metrics that are forever at odds and worth meeting early: **throughput** (the total number of tasks completed per unit time) and **latency** (the time for a single task from submission to completion). Batching work up can raise throughput, but the task at the tail of the queue has to wait longer; making the queue deeper lets you ride out bursts, but the tasks that join it also wait longer. There is no answer to this trade-off that fits everywhere; only your business's latency budget gets to decide. The deeper treatment of measurement methods and percentiles (P99, say) belongs to the performance volume, Volume 6; here we just introduce the names.

Let us condense all of the above into four questions. If you cannot answer one of the four, it is time to slow down: is the bottleneck real (talk with data, not with feelings)? What fraction of it is serial? Can the behavior of the concurrent version be verified (tooling, assertions, stress tests — have at least two of the three ready)? Can the team carry this complexity (writing it is only half the job; keeping it alive is what counts)?

One plain-spoken word to finish: the complexity that concurrency brings in is real. Data races, deadlocks, spurious wakeups on condition variables, object lifetimes — every one of them is a master at being hard to reproduce and hard to debug. **If a single thread can solve the problem, we do not reach for concurrency just to show off. There is exactly one legitimate reason to go concurrent: a single thread genuinely is not enough anymore.**

## Where This Volume Goes

Chapter 0 is the chapter in front of you right now, and two more articles follow. The next one has you write a two-threaded piece of code that goes wrong with your own hands, and puts this volume's first tool, ThreadSanitizer, into them; the one after that puts a price tag on the cost of OS threads. After that there are seven more chapters, walking all the way from thread primitives to coroutines and actors.

The volume's chapter map, what each chapter covers, how the exercise system is arranged, and which reading path suits you are all laid out on [the volume home page](../). If you get lost in some chapter, go back to that page, glance at where you are, and you will find your way back.

## Exercises

### Exercise 1: Point Out the Blocking Point

Go back to that acquisition device main loop above. In two or three sentences of your own, answer: which step is the blocking one, which other steps get dragged down when it blocks, and if you were allowed to move exactly one of these jobs onto another flow of execution, which one would you move and why. This exercise involves no real code; what it trains is the eye for seeing structure clearly.

### Exercise 2: Three True-or-False Questions

Decide whether each is true or false, and give a one-sentence reason for each — every reason has to start from the distinction between concurrency and parallelism:

- On a single-core machine, writing concurrent programs is meaningless.
- A parallel program is necessarily also a concurrent program.
- Coroutines can raise the degree of parallelism on a single-core machine.

A hint for you: for the second one, think about data parallelism such as SIMD; for the third, think about what exactly coroutines give you.

### Exercise 3: Prescribe for Four Scenarios

For the four scenarios below, which ones are worth introducing concurrency into, and what form should it take? Write one or two sentences of judgment for each:

- A stretch of floating-point computation whose dependencies are thoroughly tangled, already running in an acceptable amount of time on a single thread.
- An acquisition device that reads sensors, reports over the network, and refreshes the screen, where blocking in any one step drags down the other two.
- An echo service that has to serve tens of thousands of TCP connections at once.
- Batch-resizing ten thousand images, with no dependencies between the images at all.

We planted a signpost for the third one earlier in this article; think about which chapter it points to before you put pen to paper. For the fourth, also write one sentence about how the data should be chunked.

### Optional: Work Through Amdahl Yourself

If you have time to spare, set f to 0.8 and work out the speedup on 8 cores along with the theoretical ceiling, then swap N for 1024 and watch how the numbers react. One last question: to double the ceiling, how far down does the serial fraction have to be pushed? A calculator is enough — it is three lines of arithmetic.

## Next Steps

The worldview is now laid out; the rest is handed over to practice. In the next article we go after the nastiest problem in correctness: an innocuous-looking counter drags trouble in with it, and then we have TSan catch it red-handed on the spot. [Data Races and ThreadSanitizer, Lesson One](./02-data-race-and-tsan.md) covers the definition, the tool, and the fix, and the first principle lands on solid ground there as well.

## References

- [Concurrency Is Not Parallelism (slides) — Rob Pike, Heroku Waza, 2012](https://go.dev/talks/2012/waza.slide)
- [Concurrency Is Not Parallelism (video) — Rob Pike](https://www.youtube.com/watch?v=oV9rvDllKEg)
- [Amdahl's Law — Wikipedia](https://en.wikipedia.org/wiki/Amdahl%27s_law) — the common formulation; the sidebar links the original paper and its reprint
- [Validity of the Single Processor Approach to Achieving Large Scale Computing Capabilities — G. Amdahl, AFIPS '67](https://doi.org/10.1145/1465482.1465560) — the original paper, no equations and only one figure
- [Reevaluating Amdahl's Law — J. Gustafson, CACM 31(5), 1988](https://doi.org/10.1145/42411.42415)
- [Gustafson's Law — Wikipedia](https://en.wikipedia.org/wiki/Gustafson%27s_law)
- [The Free Lunch Is Over — Herb Sutter, Dr. Dobb's Journal, 2005](https://web.archive.org/web/*/http://www.gotw.ca/publications/concurrency-ddj.htm) — the landmark article on the clock-speed wall and the turn to multiple cores
- [C++ Concurrency in Action, 2nd ed — Anthony Williams, Manning, 2019](https://www.manning.com/books/c-plus-concurrency-in-action-second-edition) — Chapter 1 has a qualitative discussion of thread overhead and suitable use cases
