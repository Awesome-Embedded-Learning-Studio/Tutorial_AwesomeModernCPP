---
chapter: 13
difficulty: intermediate
order: 10
platform: host
reading_time_minutes: 16
tags:
- cpp-modern
- host
- intermediate
- 工具链
- 链接器
title: "Library Search Logic: How Libraries Are Found at Link Time and Run Time"
description: "The order of -L directories at link time, tested with two same-named libraries and a fake libz shadowing the system one to prove -L outranks the default directories; with a .a and a .so side by side in one directory the dynamic one wins, and -l: or a full path overrules; the three link-time not-found failures walked scenario by scenario, the single-pass scan caught on a --trace-symbol trace; at run time a different party takes over the search, with LD_PRELOAD, LD_LIBRARY_PATH, RUNPATH, the ldconfig cache, and the trusted directories each proven rank by rank, and RPATH versus RUNPATH settled by one tag; the three-piece set's three names each governing a stretch, with what NEEDED records when the SONAME is unset tested too; closing with the CMake perspective on BUILD_RPATH and find_library."
cpp_standard: [11, 14, 17, 20]
translation:
  source: documents/compilation/10-library-search-logic.md
  source_hash: b6180b309d0f57fe8e0ed4a91f73bf41de6a58d1a6f8a656aeba1360ccf352f5
  translated_at: '2026-10-13T04:13:54+00:00'
  engine: anthropic
  token_count: 8600
---

# Library Search Logic: How Libraries Are Found at Link Time and Run Time

At the close of part 09 we handed over to today three questions that come up on the engineering side every single day: in what order does the linker actually search for libraries? Does -lcalc go after libcalc.so or libcalc.a? And at run time, where do LD_LIBRARY_PATH and the system directories rank against each other? These questions reach back even further. Back in part 01, passing by the topic of libraries, we asked a string of our own: you link the library and it still says not found; between static and dynamic, which one stands in front; the library is plainly there and it still reports it missing. We promised back then that when the day came, we would walk through them one scenario at a time. Today is the day we make good: link time runs along one line, run time along another, each with its own search order, each answering to its own errors.

For the specimen we keep the name calc, but swap the contents for a setup that reports its own identity: the function inside the library is simply called answer, and whichever library got linked in, whichever got loaded in, the return value announces its number on the spot:

```c
// main.c — the caller: whichever number answer returns, that is the library that got picked
#include <stdio.h>
extern int answer(void);
int main(void) { printf("answer=%d\n", answer()); return 0; }
```

I have a whole row of these libraries prepared, with answer running from 1 up to 42, the only difference between them being the return value — just the right marker for telling libraries apart. The usual environment disclosure: my experiments all ran in WSL2, the toolchain is still the GCC 16.2.1 plus binutils 2.47 combination, glibc is 2.44, and CMake is 4.4.3, and every piece of output in this article is really run. If your versions differ, details like the wording of error messages and the shape of paths may drift a little, but the mechanics themselves have not changed in years.

## Link Time: The Order of -L Directories

The linker holds a ready-made list: every -L you write adds one directory to the list, ranked just as they were written down. Don't take my word for it — let's set out two same-named libraries with different contents:

```text
$ ar rcs dir_a/libcalc.a calc_a.o      # answer returns 1
$ ar rcs dir_b/libcalc.a calc_b.o      # answer returns 2
$ gcc main.o -Ldir_a -Ldir_b -lcalc -o app_a && ./app_a
answer=1
$ gcc main.o -Ldir_b -Ldir_a -lcalc -o app_b && ./app_b
answer=2
```

The two commands differ only in the order the two -L flags are written in: in the winning run, the version picked is the one in dir_a returning 1, and the moment we swap the two -L flags, the one returning 2 takes the job. The linker searches the directories one by one from left to right, the first directory where the library name assembles gets the library, and the directories ranked behind it are never looked at again.

Besides the -L flags we write, the linker also carries a short string of default directories of its own — we asked it with ld --verbose: the built-in list runs /usr/x86_64-pc-linux-gnu/lib64, /usr/lib, /usr/local/lib, /usr/x86_64-pc-linux-gnu/lib and so on, sitting behind all the -L entries. We should not leave this at words alone, so we shadowed it once with a library that really exists on the system: I built a fake libz.so whose entire file exports only one symbol, answer, put it into a shadow directory, and had main ask for the library with -lz:

```text
$ gcc -shared -fPIC -o shadow/libz.so calc_a.c
$ gcc main.o -Lshadow -lz -o app_shadow && ./app_shadow; echo $?
./app_shadow: symbol lookup error: ./app_shadow: undefined symbol: answer
127
$ ldd app_shadow | grep libz
	libz.so => /usr/lib/libz.so (0x00007779df4d8000)
```

This round we have to tell in two halves. Look at link time alone: the real libz.so sits in the default directories, yet the link went through, and answer resolved at link time onto the fake one in shadow — the -L directories do rank in front of the default directories, and that much is verified. Now look at run time: the dynamic linker took the name libz.so from NEEDED, but what got loaded into the process was the real libz from /usr/lib, which has no answer inside it, and main's very first call fell over on the spot. Same name, yet the two parties handling the search each picked their own file. We'll leave this accident on record here and come back to it when we reach run time.

## Side by Side under One Name: .so Ranks Ahead of .a

With the directory settled, the files inside one and the same directory have their own hierarchy too. Part 06 left an opening: when libfoo.a and libfoo.so lie side by side in the same directory, which one does -lfoo actually take? Let's lay out a real pair, the static library's answer returning 1, the dynamic one's returning 7:

```text
$ cp dir_a/libcalc.a mixed/libcalc.a
$ gcc -shared -fPIC -o mixed/libcalc.so calc_so.c    # answer returns 7
$ gcc main.o -Lmixed -lcalc -o app_mixed && LD_LIBRARY_PATH=mixed ./app_mixed
answer=7
$ readelf -d app_mixed | grep libcalc
 0x0000000000000001 (NEEDED)             Shared library: [libcalc.so]
$ gcc main.o -Lmixed -l:libcalc.a -o app_force_a && ./app_force_a
answer=1
$ nm app_force_a | grep answer
0000000000001161 T answer
$ gcc main.o ./mixed/libcalc.a -o app_path_a && ./app_path_a
answer=1
```

The dynamic library wins: when the linker enters the directory to look for -lcalc candidates, libcalc.so is already there, and the .a never even gets the chance to be opened; only once the .so is gone does the .a move up to the top. The precedence between the two suffixes that part 06 promised an answer for, we now hand it in: dynamic in front, static behind. If you want to jump this queue and call for the .a by name, you can go through -l:libcalc.a, and the full-path spelling works too — we ran each of them once, and in both answer came back as 1; and this time answer shows up as a T with an address, the machine code merged into the executable, exactly the familiar static-linking story part 06 told. As part 08 also showed, if you write the library file's path directly, the linker takes that too, and it is spared picking through a roomful of .a and .so files — with that, the whole method of choosing is laid out complete.

## Three Ways to Not Find It at Link Time

With order and suffix both standing, it is time to make good on part 01's promise: you linked the library, and it still says not found. Let's start with the first scenario: the library is placed in the wrong spot. When part 05 walked through hunting down undefined references, we asked whether some .o had been forgotten to attach, or whether the definition lived in a library never written. The library is written, the .o is attached, and a third way to trip is still waiting: the library is ranked on the command line in front of the .o that references it.

```text
$ gcc -Ldir_a -lcalc main.o -o app_order
/usr/bin/ld: main.o: in function `main':
main.c:(.text+0x5): undefined reference to `answer'
collect2: error: ld returned 1 exit status
$ gcc main.o -Ldir_a -lcalc -o /dev/null -Wl,--trace-symbol=answer
/usr/bin/ld: main.o: reference to answer
/usr/bin/ld: dir_a/libcalc.a(calc_a.o): definition of answer
```

We only moved main.o to the left of -lcalc and the link went through, without touching a single byte of the library — riding exactly on the ordering part 06 laid down: the library goes behind what references it. Behind this sits the linker's single-pass scan: it walks the command line from left to right and never revisits a file it has passed. By the time it reaches libcalc.a, it glances at the list of U symbols hanging in its hand; main.o's reference has not arrived yet, the list is empty, the index lookup comes up for nothing, so not one member gets extracted; by the time main.o then puts answer onto the U list, that .a file up ahead is long gone. The ordering between libraries themselves we have to keep just the same: a library references other libraries too — if library A uses a symbol of library B, then -lB has to sit to the right of -lA, the same principle as writing the .o before the library. If two libraries use each other's symbols, one side's symbols are bound to come up empty — that is the circular dependency part 06 noted without expanding; we have two ways out. The first is to write the -l flags a second time, the final pass making up the symbols the earlier round missed, which laid out comes to -lA -lB -lA. The other uses the --start-group and --end-group flags: write the two libraries between the flags, and the linker will scan the group over and over until no new member can be extracted.

Words alone are always thin, and the linker can confess on its own: add the --trace-symbol=answer flag to the link command and it reports every stop that symbol made along the way. In the successful link above, reference and definition arrive in the trace one after the other, and the member gets extracted. Move the library to the front and run it again, and the trace keeps only the reference line, definition never appears again, and the error is just as before.

One piece of the truth I have to report to you: with the same misplacement, when we swapped the .a for a .so, on this GCC 16.2.1 setup the link actually succeeded, and answer still printed 7. A dynamic library knows no extraction — the linker registers it whole, and when a later .o references it, the registration is already on file, so the victim in this scenario is mainly the .a. If the same command on your side reports undefined reference, chances are your distribution quietly adds --as-needed to gcc by default: it only registers libraries somebody already wanted at that moment, and a .so ranked in front of its references gets dropped entirely. The link line this setup of ours produced carries no such flag — I dug through the real link line with gcc -###, and it showed up in neither direction's output.

On to the second scenario: the name breaks the convention. I copied the library into a naming directory and stripped the lib prefix while I was at it:

```text
$ cp dir_a/libcalc.a naming/calc.a
$ gcc main.o -Lnaming -lcalc -o app_naming
/usr/bin/ld: cannot find -lcalc: No such file or directory
/usr/bin/ld: note to link with naming/calc.a use -l:calc.a or rename it to libcalc.a
collect2: error: ld returned 1 exit status
```

You saw this same error in part 06; today we pay back the angle owed back then: why searching this directory comes up empty anyway. The candidate names we ask it to look for are only libcalc.so and libcalc.a, and the file in naming is called calc.a, so it assembles against neither candidate. The linker is not actually blind to this file — it simply is not on the candidate list under any entry. The two routes the note hands over each have their place: -l:calc.a bypasses the name assembly, and changing the name back to libcalc.a puts it back on the list.

The third scenario is the least deserved of all: the library is lying right in our current directory, and we did not write a single -L:

```text
$ gcc main.o -lcalc -o app_bare
/usr/bin/ld: cannot find -lcalc: No such file or directory
collect2: error: ld returned 1 exit status
```

The libcalc.a and libcalc.so in mixed are both right there, but the linker's directory list only recognizes what -L adds and that string of default directories it carries; the directory we currently stand in cannot squeeze in. To let it in, writing one -L. is enough. When you hunt down a cannot find someday, there are only three directions to check: does the name follow the convention? Is the directory on the list? Is the library ranked behind what references it?

## Run Time: a Different Party Takes over the Search

Remember the shadow round? At link time the fake libz in the -L directory got picked; at run time what got loaded in was the real libz from /usr/lib. The question part 08 left behind can be answered in full here: why, when the link went so smoothly, does run time still need LD_LIBRARY_PATH waiting on it? Because the one handling the library search in the two stages is simply not the same party. At link time it is the linker that steps forward; it knows -L and its own default directories, and once the link is done, the -L information has not landed anywhere in the executable. At run time it is the dynamic linker that steps forward — the true identity behind the interpreter at the end of the file output in part 01, the same party that read NEEDED line by line in part 08; it is all the same one. It has never seen our link command from start to finish, so naturally it does not know -L either. The LD_LIBRARY_PATH=. we kept writing in part 09 — now you can say clearly what it does: it slips the current directory into the environment variable tier.

The tiers it recognizes, we measured tier by tier. We made SONAME's acquaintance in part 08: the formal name the library registers for itself. The three libraries prepared this time differ in contents but all register the SONAME libcalc.so.1, placed into three separate directories, with answer returning 10, 20, and 30. The program has a run-time search path planted in it, by way of the -rpath linker flag: the linker writes the path into the dynamic section, and it becomes a RUNPATH. At link time we write the path into rpath_dir directly, to keep the link-time choice question from dragging back in; NEEDED registers only the SONAME, and by run time it no longer remembers which file was linked:

```text
$ gcc -shared -fPIC -Wl,-soname,libcalc.so.1 -o rpath_dir/libcalc.so.1 calc10.c
$ gcc main.o ./rpath_dir/libcalc.so.1 -Wl,-rpath,/tmp/…/rpath_dir -o app_rpath
$ readelf -d app_rpath | grep -E "libcalc|PATH"
 0x0000000000000001 (NEEDED)             Shared library: [libcalc.so.1]
 0x000000000000001d (RUNPATH)            Library runpath: [/tmp/…/rpath_dir]
$ ./app_rpath
answer=10
$ LD_LIBRARY_PATH=/tmp/…/lp_dir ./app_rpath
answer=20
$ LD_PRELOAD=/tmp/…/preload/libcalc.so.1 ./app_rpath
answer=30
```

(I trimmed the long paths down to /tmp/…; the rest is verbatim.) The three result lines we take one at a time: run bare, what gets loaded is the version in the rpath_dir that RUNPATH points at, returning 10; set LD_LIBRARY_PATH as well, and the one returning 20 pushes RUNPATH down instead; finally bring in LD_PRELOAD, and not even the search happens — the preloaded library is the first one mapped into the process, and it registers the SONAME libcalc.so.1 too, so the NEEDED line finds its owner on the spot and the entire search behind it never takes place. We counted with LD_DEBUG=libs: not a single find library=libcalc line in the trace — the count came out 0.

The full order is nothing to memorize: the LD_DEBUG=libs search trace labels each tier's name at the end of its line. The program's two dependencies each got searched once, we filtered twice with a grep pattern, and the lines that remain we read from the top:

```text
$ LD_DEBUG=libs LD_LIBRARY_PATH=/tmp/…/lp_dir ./app_rpath 2>&1 | grep -E "find library|search path=.*(LD_LIBRARY_PATH|RUNPATH)|search cache|trying file=/usr/lib/libc"
   1648884:	find library=libcalc.so.1 [0]; searching
   1648884:	 search path=/tmp/…/lp_dir/glibc-hwcaps/x86-64-v3:/tmp/…/lp_dir/glibc-hwcaps/x86-64-v2:/tmp/…/lp_dir		(LD_LIBRARY_PATH)
   1648884:	find library=libc.so.6 [0]; searching
   1648884:	 search path=/tmp/…/lp_dir		(LD_LIBRARY_PATH)
   1648884:	 search path=/tmp/…/rpath_dir/glibc-hwcaps/x86-64-v3:/tmp/…/rpath_dir/glibc-hwcaps/x86-64-v2:/tmp/…/rpath_dir		(RUNPATH from file ./app_rpath)
   1648884:	 search cache=/etc/ld.so.cache
   1648884:	  trying file=/usr/lib/libc.so.6
```

(That string of digits at the head of each line is the process id; yours will not come out the same.) Reading on down: libcalc hits on the LD_LIBRARY_PATH tier right away, so its search stops at the second line; libc draws a blank on that same tier, next it is RUNPATH's turn, and the tier after that is search cache=/etc/ld.so.cache — libc happens to be on the cache's list, and it lands on /usr/lib/libc.so.6 in one step. As for the glibc-hwcaps/x86-64-v3 style subdirectories tucked inside the paths, that is glibc's scheme, since 2.33, for picking an optimized build by CPU instruction-set tier; we have no use for it in this piece — just recognize the directory itself. And if the cache comes up empty too? The last tier is the system's default directories, whose original shape we'll look at in a moment.

This order — LD_LIBRARY_PATH outranking RUNPATH — older references often write the other way around. The authors of those references are often not wrong; what really changed generations is the tag: there are two tags for writing a run-time search path into the dynamic section, the old-style one named DT_RPATH and the new-style one named DT_RUNPATH. The linker today produces the new style by default, the default being held in place by the --enable-new-dtags flag, and adding --disable-new-dtags falls back to the old style. The difference that lands on search order is a single line: RPATH ranks in front of LD_LIBRARY_PATH, RUNPATH behind it. Let's relink the same program with the old-style tag:

```text
$ gcc main.o ./rpath_dir/libcalc.so.1 -Wl,--disable-new-dtags,-rpath,/tmp/…/rpath_dir -o app_oldrpath
$ readelf -d app_oldrpath | grep -E "PATH"
 0x000000000000000f (RPATH)              Library rpath: [/tmp/…/rpath_dir]
$ LD_LIBRARY_PATH=/tmp/…/lp_dir ./app_oldrpath
answer=10
```

Same environment variable, same two libraries, and this time the winner is 10 — RPATH pushed LD_LIBRARY_PATH back, exactly the reverse of the RUNPATH round. If you ever see opposite experimental results on two machines, most likely it is this one tag's difference; one look with readelf -d settles it.

## Right There, Yet Reported Missing

Part 08 left behind an even more galling situation: the file is plainly there, yet it insists it cannot find it. Let's stage this scene. This time we planted no RUNPATH in the program; the library lies in the rpath_dir directory next door, and it is the very one linked against:

```text
$ gcc main.o ./rpath_dir/libcalc.so.1 -o app_norpath
$ ./app_norpath; echo $?
./app_norpath: error while loading shared libraries: libcalc.so.1: cannot open shared object file: No such file or directory
127
$ LD_DEBUG=libs ./app_norpath 2>&1 | grep -E "find library|search cache|search path=.*system|trying file=/usr/lib/libcalc|no such file|error while"
   1648898:	find library=libcalc.so.1 [0]; searching
   1648898:	 search cache=/etc/ld.so.cache
   1648898:	 search path=/usr/lib/glibc-hwcaps/x86-64-v3:/usr/lib/glibc-hwcaps/x86-64-v2:/usr/lib		(system search path)
   1648898:	    (no such file)
   1648898:	    (no such file)
   1648898:	  trying file=/usr/lib/libcalc.so.1
   1648898:	    (no such file)
./app_norpath: error while loading shared libraries: libcalc.so.1: cannot open shared object file: No such file or directory
```

Count through the trace one by one: we planted no RUNPATH, we set no LD_LIBRARY_PATH, the cache holds no shadow of it either, and at last the search reaches the system search path tier — the system's default directories, which the documentation calls the trusted directories — and all the way down /usr/lib we see nothing but (no such file). And the current directory? Not one tier holds a place for it, in exactly the predicament of scenario three at link time: over there, without adding -L it cannot enter the list; over here there is no -L at all, still less anyone to carry it along. If we want a library like this to be found, there are three roads. The first is to move the file into the trusted directories — root privileges are unavoidable — and once moved, ldconfig has to be called in: glibc's family tool for keeping the cache, which scans the trusted directories and the directories registered in /etc/ld.so.conf.d and writes the name-to-path mapping into /etc/ld.so.cache. The second road is LD_LIBRARY_PATH or RUNPATH, slipping the directory into one tier of the search. The third is to write the path into RUNPATH at release time. My WSL has no passwordless privilege escalation, so I did not actually do this step; let's just take a read-only look at what the cache looks like:

```text
$ ldconfig -p | grep "libz\.so$"
	libz.so (libc6,x86-64) => /usr/lib/libz.so
```

The cache tier genuinely getting used, last section's libc trace already acted it out: after the search cache line it drops straight to trying file=/usr/lib/libc.so.6, hitting in one step. In the not-found round, that same line is followed by (no such file), the trusted directories at the bottom of the barrel come up empty too, and only then does the error touch ground. The link-time cannot find and the run-time cannot open shared object file, we have now seen both in this piece: one error's header is /usr/bin/ld, the other's is the program's own name — which of the two is speaking? One glance at the header tells them apart.

## The Three-Piece Set: Three Names, Each Governing a Stretch

With the ordering told, let's also fill in today how the three-piece set part 08 left us gets arranged. The real file's name carries the full version number — what we build is libcalc.so.1.2.3, and the trade calls it the realname. The second name is the formal one, libcalc.so.1, which we register with the soname flag, with a symlink fitted to match. The third name is the one for development-time linking, in the trade's words the linker name: the symlink libcalc.so, which is exactly what the -l name assembly in part 06 assembles up:

```text
$ gcc -shared -fPIC -Wl,-soname,libcalc.so.1 -o trio/libcalc.so.1.2.3 calc42.c
$ ln -s libcalc.so.1.2.3 trio/libcalc.so.1 && ln -s libcalc.so.1 trio/libcalc.so
$ readelf -d trio/libcalc.so.1.2.3 | grep SONAME
 0x000000000000000e (SONAME)             Library soname: [libcalc.so.1]
$ gcc main.o -Ltrio -lcalc -o app_trio
$ readelf -d app_trio | grep libcalc
 0x0000000000000001 (NEEDED)             Shared library: [libcalc.so.1]
$ LD_LIBRARY_PATH=trio ./app_trio
answer=42
```

Let's walk one link from start to finish and identify, one by one, which stretch each of the three names governs: -lcalc assembles the linker name libcalc.so; the linker follows the symlink to the real file, reads off its registered SONAME, and what gets written into NEEDED is libcalc.so.1; the run-time dynamic linker searches with that name, hits the symlink libcalc.so.1, and the symlink points back to the real file — that is how the 42 got in. On an upgrade you swap the real file and move the symlinks, and not one byte of the executable has to change; the benefit part 08 spoke of lands on the files as exactly this arrangement. What each of the version number's three parts records, and which part what kind of change should move — those we hand to the ABI article.

Since NEEDED records the SONAME, when the SONAME changes, does NEEDED follow along? We relink one copy registering the soname as libcalc.so.9, the real file's name unchanged:

```text
$ gcc -shared -fPIC -Wl,-soname,libcalc.so.9 -o soname/libcalc.so.1.0.0 calc42.c
$ gcc main.o ./soname/libcalc.so.1.0.0 -o app_soname9
$ readelf -d app_soname9 | grep calc
 0x0000000000000001 (NEEDED)             Shared library: [libcalc.so.9]
$ ./app_soname9
./app_soname9: error while loading shared libraries: libcalc.so.9: cannot open shared object file: No such file or directory
$ ln -sf libcalc.so.1.0.0 soname/libcalc.so.9
$ LD_LIBRARY_PATH=soname ./app_soname9
answer=42
```

NEEDED did follow along, and it recognizes only the SONAME — what the real file happens to be called, NEEDED does not care; with no file named libcalc.so.9 in the directory, the run-time search comes up empty just the same, and adding one same-named symlink brings it back to life. And when the SONAME is left out entirely? The behavior splits in two, and we measured both. For a library found through -l, NEEDED records the file name — the hand-built libmyutil.so in part 08 registered exactly libmyutil.so, and here is where that came from. For a library linked by full path, NEEDED records, of all things, the entire slash-bearing path:

```text
$ gcc -shared -fPIC -o soname/libcalc_noson.so.5 calc42.c
$ gcc main.o ./soname/libcalc_noson.so.5 -o app_noson
$ readelf -d app_noson | grep calc
 0x0000000000000001 (NEEDED)             Shared library: [./soname/libcalc_noson.so.5]
```

A slash-bearing dependency name also drags out another run-time behavior: when the dynamic linker sees a slash in the name, it stops searching and opens it directly as a literal path relative to the launch directory. We ran it from a different directory, and no amount of LD_LIBRARY_PATH could save it. That is why libraries released in earnest all register a SONAME, settling the name as a bare, slash-free one, so the search machinery can get a handhold on it.

## The Same Problem on Windows

Everything in this piece has been the ELF side of the story. The broad structure on Windows is the same, the details everywhere differ; of the comparison part 06 promised, today we take only a short stretch. At link time, the MSVC family looks for .lib: the static library's file is one, and so is the import library fitted to a DLL — one suffix carrying two identities. The directories searched come from the LIB environment variable, and the name assembly has no lib-prefix convention: the library for foo is just foo.lib. At run time the loader looks for the DLL, and the very first place searched is the EXE's own directory, with the system directories ranked behind, and PATH sitting at the very end. In one spot the two sides are exactly reversed — look at it side by side: on our side, a library lying next to the program goes unnoticed (the cannot open round last section just acted it out); on the Windows side it is the other way around — DLL and EXE placed together is the first-class, proper road, and the loader naturally searches from the very front. That the two stages, linking and running, search separately is the broad structure both sides share; the finer points of the comparison we hand to the ABI article.

## The CMake Perspective

All this work of tuning -L by hand and writing -rpath by hand belongs to CMake in day-to-day builds. That string of run-time paths has a dedicated place to be written in CMake:

```cmake
add_library(calc SHARED calc_c.c)
add_executable(app main_c.c)
target_link_libraries(app PRIVATE calc)
set_target_properties(app PROPERTIES BUILD_RPATH "/opt/calc_runtime")
```

BUILD_RPATH governs the run-time search path of the build-tree artifacts, and we casually wrote in a /opt/calc_runtime that does not exist yet. Run with --verbose and look at the link line: it really landed in the link line's rpath flag:

```text
$ cmake --build build --verbose 2>&1 | grep "cc .*-o app"
/usr/sbin/cc … CMakeFiles/app.dir/main_c.c.o -o app  -Wl,-rpath,/opt/calc_runtime:/tmp/…/build libcalc.so
```

(The middle of the link line and the long paths I trimmed to … and /tmp/… as usual; the rest is verbatim.) Look at the paths in the link line, there are two of them: the /opt/calc_runtime we wrote sits at the head, and the build directory CMake adds on its own sits behind. That RUNPATH line in part 08 — the one said to be something CMake slips in without saying why — today the why gets filled in: seeing that the target being linked lives in the build tree, CMake slips the build directory in as well, so the app in the build tree finds the same-tree library when run bare. There is also the global CMAKE_BUILD_RPATH for adding paths across the board, used in exactly the same way.

And the -L end of things? Part 06's CMake section showed us: for a library born in the same build, CMake hands libmyutil.a's path straight to the link command, carrying both -L and the name assembly for us. If the library lives outside the project, the one that steps out to find it is find_library, and what it finds will be an absolute path:

```cmake
find_library(CALC_LIB NAMES calc PATHS ${CMAKE_CURRENT_SOURCE_DIR}/libs NO_DEFAULT_PATH)
target_link_libraries(app PRIVATE ${CALC_LIB})
```

```text
-- find_library got: /tmp/…/libs/libcalc.so
$ cmake --build build --verbose 2>&1 | grep "cc .*-o app"
/usr/sbin/cc … main_c.c.o -o app  -Wl,-rpath,/tmp/…/libs /tmp/…/libs/libcalc.so
```

In the link line, -L and -l are both gone; what gets handed over is the absolute path, and along with it the library directory gets written into RUNPATH — the built app passes when run bare, and what we measured out was exactly 42. As for the target_link_directories we did not use and the still older link_directories, the trade-off in one sentence: the former takes effect per target, the latter is a directory-level cut through everything, smearing every subdirectory; and in CMake's own recommendation the two of them rank behind find_library — a path already searched out and settled is steadier than stuffing a string of -L flags for the linker to go bumping against one by one.

## Next: Same-Named Symbols, Who Has the Final Say

Both lines run smooth by this point: on the link-time line, the linker finds the library with the -L directory list and the name-assembly convention, registering what should be registered, extracting what should be extracted; on the run-time line, the dynamic linker, in the order preload, environment variable, RUNPATH, cache, trusted directories, gathers the libraries up and maps them in. But with the libraries all inside the process, the job is only half done: when main calls answer, several export tables in the process are all open — who stands at the head? On what grounds does a replacement win? How do we guard our own symbols? And how do we narrow the opening on purpose? Part 09's preload experiment showed a corner of it; the symbol visibility article devotes its whole length to exactly this one matter.
