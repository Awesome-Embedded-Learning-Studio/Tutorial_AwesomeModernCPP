---
title: "The Lecture Notes Aren't Done Downloading — Why Can the List Already Scroll?"
description: "Starting from the fact that you can keep scrolling a list while lecture notes download, we use two timelines to figure out which things can be reordered and which must wait."
chapter: 5
order: 1
tags:
  - host
  - cpp-modern
  - beginner
  - 异步编程
difficulty: beginner
platform: host
reading_time_minutes: 6
translation:
  source: documents/vol5-concurrency/chapter01-start-concurrency/01-work-and-order.md
  source_hash: 3ddee28fa6684235cc2e7003624350b2c84593283fbb440e729fa19bf8a5139f
  translated_at: '2026-11-22T13:28:59+00:00'
  engine: glm-5.3
  token_count: 2066
---
# The Lecture Notes Aren't Done Downloading — Why Can the List Already Scroll?

"Oi, Charliechen114514, stop slacking! Get on that website and download the materials!" — back when this author was still in college, the teacher said we had to go download materials from a certain website, and this is what my classmate said to me.

Unfortunately, the campus network really wasn't great. You click download on a few-MB file, and the spinner icon at the top of the browser just keeps turning... So you grumble, "ah damn, my friend, never mind that this page is written like a pile of junk — the network is trash-speed too!" while flipping through the page... Wait a minute!

Stop. Have you noticed a counterintuitive fact? In your mental model, the CPU executes one definite flow of control. For example, when you tell the CPU: finish A first, then do B, then finally C, the CPU answers, "You got it, boss!" and buries its head in execution. But clearly, in the scene just now, **your browser was downloading something while at the same time displaying the page you were scrolling!** It looks like your computer is doing different things simultaneously! Pretty cool, right!

## Let's Write Down What Happens on Screen, and We'll See That Gut Feel Alone Can't Judge Real Concurrency

To make the telling easier, this author decides to kill off the dramatic narration and use the Mermaid diagram below to show us the order of events.

```mermaid
graph LR
    A["点下载"] --> B["列表滚动"] --> C["讲义下完"] --> D["显示完成提示"]
```

Note that this is a linear order diagram: we are talking about the order of **the moments at which things happen**, while the unfolding of each thing is a process. For example, the instant from clicking download to the browser issuing the request is merely the time it takes for the button to fire the network-request callback, right? But the download process itself, that is A through C, is backed by the time the network takes to haul the content from a remote machine over to here. **It does not stop us from letting the browser's list scroll.**

What if the resource being delivered is itself tiny? There isn't much content; your hand has barely moved onto the scroll wheel and the download may already be done, so the order you observe is yet another one.

```mermaid
graph LR
    A["点下载"] --> B["讲义下完"] --> C["显示完成提示"] --> D["列表滚动"]
```

Now things look sequential again! But although they look sequential, the more accurate statement is that the time consumed by the download is smaller than the time the browser takes to react and scroll. You see, that likewise guarantees the content finishes downloading before my list starts scrolling.

So, judging from the observations above, it doesn't hold up if we reason about whether concurrency is needed — or whether concurrency is happening — by **leaning only on event milestones rather than on the process of things unfolding**. The key mental model sits in the comparison I stressed two points back: "the act of downloading does not hinder, nor should it hinder, us from letting the browser's list scroll."

> If it still hasn't clicked, that's fine — let me say it formally, one step at a time: the download receives some content, and the list moves down a bit. More content arrives, and it moves down a bit again. **Within the same stretch of time, both things are making progress — that is concurrency itself.** Neither one has to yield the whole interval to the other.

You absolutely must understand this — it is the whole point of **learning concurrent programming itself**. It is **not merely about making the program faster**, but about **letting the program itself escape unnecessary waiting**. Whether we later discuss concurrency optimization based on divide-and-conquer or on logically orthogonal tasks (our example, for instance), it all sets out from this goal. Applying concurrency always starts from the thought "shit bro, making the CPU wait here is dumb — yield this time and hand it to someone else to work with!"

> In terms of the example above: does concurrency make the download itself any faster? No — the moment you get the lecture notes may not be one bit earlier. Fair enough: the amount of content to transfer is unchanged, the network speed is unchanged, and it takes as long as it takes. What concurrency buys is not speed, **but the ability to do an orthogonal, unrelated thing alongside it**: the task at your fingertips gets attended to immediately. Under the old one-line approach, once you clicked download you just had to wait; the scroll-wheel action had to queue until the download finished before anyone dealt with it, and to you the page was no different from frozen. Being able to flip through the list right now is a tangible benefit you can feel.

You might think: why wait for a notification — just estimate three seconds and pop the "download complete" message, done? Today the network is fast, three seconds happens to be enough, and the prompt pops at exactly the right moment. Tomorrow the network slows down, the download needs four seconds, but the prompt still pops at three — one second short of done, yet the page insists it is finished. The root is the dependency we just talked about: what the prompt waits for is the download genuinely completing — whether the thing happened — not how many notches the clock has advanced. As long as the later step depends on the download's result, we have to wait for completion to really happen; guess with a fixed number of seconds, and there will always be a day the guess is wrong.

## Reference Resources

<ReferenceCard title="参考文献">

- [Go Concurrency Patterns, Rob Pike](https://go.dev/talks/2012/concurrency.slide) — introduces concurrency from how work is organized, then discusses whether executions overlap.

</ReferenceCard>
