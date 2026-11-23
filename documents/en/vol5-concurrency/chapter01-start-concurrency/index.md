---
title: "Unit 1: Getting a Program to Run Concurrently"
description: "Know concurrency from everyday examples, understand the CPU and threads through timelines, then get hands-on with starting, waiting for, and stopping threads."
translation:
  source: documents/vol5-concurrency/chapter01-start-concurrency/index.md
  source_hash: 70dbdba47f1bd9dca4a8e3c0ece12402e7ba82f0a247e948cd1ee548e5d4e855
  translated_at: '2026-11-22T13:28:59+00:00'
  engine: glm-5.3
  token_count: 1057
---
# Unit 1: Getting a Program to Run Concurrently

You can start from the small everyday matter of flipping through a page while the lecture notes download, and see how several things inside a program advance together. Having drawn the timelines of the CPU and threads, we then write a first `std::thread` program, and learn how to wait for a thread to finish and how to bring it to an orderly stop.

## In This Unit

<ChapterNav variant="sub">
  <ChapterLink href="00-why-concurrency-unavoidable">Why You Can't Hide from Concurrency</ChapterLink>
  <ChapterLink href="01-work-and-order">The Lecture Notes Aren't Done Downloading — Why Can the List Already Scroll?</ChapterLink>
  <ChapterLink href="02-cpu-and-threads">How a Single CPU Core Takes Turns Doing Work</ChapterLink>
  <ChapterLink href="03-first-std-thread">A First std::thread Program</ChapterLink>
  <ChapterLink href="04-lifetime-and-ownership">Who Owns the Data's Lifetime</ChapterLink>
  <ChapterLink href="05-thread-lifetime-and-raii">Who Owns the Thread's Lifetime</ChapterLink>
  <ChapterLink href="06-jthread-and-stopping">Letting Work End in an Orderly Way: std::jthread and the Stop Trio</ChapterLink>
  <ChapterLink href="07-stop-callback-and-checklist">Registration Races and an Exit Checklist: The Stop Protocol's Fine Details</ChapterLink>
</ChapterNav>

## Optional Reading

<ChapterNav variant="sub">
  <ChapterLink href="08-when-concurrency">When Is It Worth Bringing In Concurrency</ChapterLink>
  <ChapterLink href="09-what-is-a-thread">What a Thread Is on the Machine</ChapterLink>
</ChapterNav>
