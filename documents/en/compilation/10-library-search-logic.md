---
chapter: 13
difficulty: intermediate
order: 10
platform: host
reading_time_minutes: 31
tags:
- cpp-modern
- host
- intermediate
title: "Library Search Logic: How Libraries Are Found at Link Time and Run Time"
description: "The order of -L directories at link time tested against two same-named libraries, then a fake libz shadowing the system library to watch -L outrank the default directories; with a .a and a .so side by side in one directory the dynamic one wins, -l: and the full path overruling; the three link-time not-found failures walked scenario by scenario, the single pass caught on a trace with --trace-symbol; at run time a different party takes over the finding, with LD_PRELOAD, LD_LIBRARY_PATH, RUNPATH, the ldconfig cache, and the trusted directories proven rank by rank, RPATH and RUNPATH told apart by one tag; the three-piece set with its three names each governing a stretch, and what NEEDED records when the SONAME is unset tested too; closing from the CMake perspective with BUILD_RPATH and find_library."
cpp_standard: [11, 14, 17, 20]
translation:
  source: documents/compilation/10-library-search-logic.md
  source_hash: 0aa8985d6a4233b0806918f23ed3eb1e9e46866c2aa74d460951b0729171cab1
  translated_at: '2026-10-01T08:06:53+00:00'
  engine: glm
  token_count: 4330
---

# Library Search Logic: How Libraries Are Found at Link Time and Run Time

At the end of part 09 we passed three questions from the engineering side along to today, questions you walk into every day of the week: in what order does the linker actually look for libraries? Does -lcalc go for libcalc.so or for libcalc.a? And at run time, where do LD_LIBRARY_PATH, the system directories, and the rest each rank? These questions have an earlier history of their own. Back in part 01, passing by the subject of libraries, we asked a string of our own: linked the library and it still says not found; static or dynamic, which one stands in front; the library is plainly there and it still says missing. We agreed back then that when the day came, we would walk them one scenario at a time. Today is the day we make good: link time walks one line, run time walks another, each with its own order, each answering to its own errors.

For the specimen we keep the name calc, but the contents swap for a setup that reports its own identity: the function inside the library is called answer, and whichever library got linked in, whichever got loaded in, the return value reports its number on the spot:

```c
// main.c — the caller: whichever number answer returns, that is the library that got picked
#include <stdio.h>
extern int answer(void);
int main(void) { printf("answer=%d\n", answer()); return 0; }
```

I prepared a whole row of libraries, answer's values lined up from 1 to 42; the only difference from one to the next is the return value, which makes them just right as markers for telling libraries apart. The environment disclosure as usual: my experiments all ran in WSL2, the toolchain is still GCC 16.2.1 paired with binutils 2.47, glibc is 2.44, CMake is 4.4.3, and every stretch of output in this piece was really run. If your versions differ, details like the wording of errors and the look of paths may come out differently; the mechanics themselves haven't changed in years.

## Link Time: The Order of -L Directories

The linker holds a ready-made list: every -L you write adds one directory to that list, and the order follows the order you wrote them in. Don't take that on faith — let's set out two libraries with the same name and different contents:

```text
$ ar rcs dir_a/libcalc.a calc_a.o      # answer returns 1
$ ar rcs dir_b/libcalc.a calc_b.o      # answer returns 2
$ gcc main.o -Ldir_a -Ldir_b -lcalc -o app_a && ./app_a
answer=1
$ gcc main.o -Ldir_b -Ldir_a -lcalc -o app_b && ./app_b
answer=2
```

(The comments at the ends of the lines are mine; the commands were really run.) The two commands differ in one thing only, the order the two -L flags are written in: in the winning run, what got picked was the return-1 version from dir_a; swap the two -L flags, and the return-2 one takes the post. The linker searched the directories one by one, left to right; the first directory that could assemble the library's name got the library, and the directories behind it were never looked at again.

Besides the -L flags we write, the linker carries a short string of default directories of its own. We asked it with ld --verbose: the built-in list runs /usr/x86_64-pc-linux-gnu/lib64, /usr/lib, /usr/local/lib, /usr/x86_64-pc-linux-gnu/lib, padded in behind all the -L directories. That claim shouldn't stay on the lips either, so we ran a shadowing round against a library the system really has: I built a fake libz.so whose entire file exports one thing, answer; we put it into the shadow directory, and main asks for the library by -lz:

```text
$ gcc -shared -fPIC -o shadow/libz.so calc_a.c
$ gcc main.o -Lshadow -lz -o app_shadow && ./app_shadow; echo $?
./app_shadow: symbol lookup error: ./app_shadow: undefined symbol: answer
127
$ ldd app_shadow | grep libz
	libz.so => /usr/lib/libz.so (0x00007779df4d8000)
```

This round has to be told in two halves. Link time alone: the real libz.so was lying in the default directories and the link passed anyway — answer got resolved at link time onto the fake in shadow, so the -L directories standing in front of the default directories, that much is verified. Then run time: the dynamic linker takes the name libz.so from NEEDED, but what it loads into the process is the real libz from /usr/lib, which has no answer inside, and main's first call goes off the road on the spot. The name is one and the same; the two parties doing the finding each picked their own file. We file this accident right here and come back to it when we reach run time.

## Same Name, Both Present: .so Ranks Ahead of .a

With the directory settled, the files inside one directory have rules of their own. Part 06 left an opening: libfoo.a and libfoo.so lying side by side in the same directory — which one does -lfoo actually take? Let's set out a real pair, the static library's answer returning 1 and the dynamic library's returning 7:

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

The winner is the dynamic library: the linker enters the directory to look for -lcalc's candidates, libcalc.so is already there, and the .a never so much as gets opened; only when the .so is absent does the one on top become the .a. The part 06 question of which of the two suffixes ranks first — the answer falls due now: dynamic in front, static padding behind. To step over that order and name the .a outright, you can go through -l:libcalc.a; writing the full path works too. We ran both, answer came back to 1 each time, and this time answer got the address-carrying T — the machine code merged into the executable file, exactly the old static-linking routine part 06 walked through. Part 08 also showed us that writing the library file's path directly gets accepted just the same, and it spares the linker from picking through a roomful of .a and .so; with that, the rules of the picking are fully told.

## The Three Link-Time Not-Found Failures

With order and suffix both standing, the turn has come to make good on part 01's promise: the library got linked, and it still says not found. We start from the first scenario, the library sitting in the wrong position. Back when part 05 worked through the undefined reference investigation, we asked: was it some .o forgotten, or the definition living in a library that never got written? The library is written, the .o is attached, and a third failure was still waiting: the library placed on the command line ahead of the .o that references it.

```text
$ gcc -Ldir_a -lcalc main.o -o app_order
/usr/bin/ld: main.o: in function `main':
main.c:(.text+0x5): undefined reference to `answer'
collect2: error: ld returned 1 exit status
$ gcc main.o -Ldir_a -lcalc -o /dev/null -Wl,--trace-symbol=answer
/usr/bin/ld: main.o: reference to answer
/usr/bin/ld: dir_a/libcalc.a(calc_a.o): definition of answer
```

We did one thing — moved main.o to the left of -lcalc — and the link passed; not one byte of the library was touched. What carried it was exactly the ordering part 06 laid down: libraries go after the references. Behind it sits the linker's single pass: it walks the command line once, left to right, and files already walked past are never revisited. When libcalc.a's turn came, it glanced at the U entries hanging unresolved in its hand; main.o's reference had not arrived yet, the list was empty, the index got consulted for nothing, and not a single member was pulled out. By the time main.o hung answer onto U, that .a file up front had long since gone by. The ordering between libraries we keep by the same rule: libraries reference other libraries themselves, and when library A uses library B's symbols, -lB must stand to the right of -lA — the same principle as .o files written in front of the libraries. If two libraries use each other's symbols, one side's symbols will always come up short; that is exactly the circular dependency part 06 noted without unfolding, and we have two ways out. The first is to write the -l flag a second time; the last pass picks up the symbols the earlier round dropped, and the line reads -lA -lB -lA. The other goes through the switches --start-group and --end-group: the two libraries written between the switches, and the linker sweeps the group again and again until no new member can be pulled out.

Talk with nothing behind it never convinces, and the linker can confess on its own: add the --trace-symbol=answer switch to the link command, and it reports every station this symbol passed through. In the successful link above, the trace shows reference and definition arriving one after the other, and the member got pulled out. Move the library up front and run it again, and only the reference line is left in the trace; the definition never appears again, and the error comes back the same as ever.

There is a fact I owe you besides: with the same misordering, swap the .a for a .so, and on this GCC 16.2.1 of ours the link actually succeeded — answer still came out 7. A dynamic library has no extraction to speak of: the linker registers the whole thing, so when a .o farther down the line references it, the registration is already in hand; the victim of this scenario is chiefly the .a. If the same command on your machine reports undefined reference, the odds are your distribution patched --as-needed into gcc by default: it registers only the libraries somebody has already asked for at that moment, and a .so standing ahead of the references gets dropped whole. Our own link command carries no such switch; I turned up the real link line with gcc -###, and it appeared in neither direction's output.

On to the second scenario: the name fell out with the convention. I copied the library into the naming directory and plucked the lib prefix off along the way:

```text
$ cp dir_a/libcalc.a naming/calc.a
$ gcc main.o -Lnaming -lcalc -o app_naming
/usr/bin/ld: cannot find -lcalc: No such file or directory
/usr/bin/ld: note to link with naming/calc.a use -l:calc.a or rename it to libcalc.a
collect2: error: ld returned 1 exit status
```

You saw this same error in part 06; today we pay back the angle that was owed: why searching this directory is searching for nothing. The candidate names it goes looking for are only libcalc.so and libcalc.a, while the file in naming is called calc.a, so it assembles against neither candidate. In truth the linker is not blind to the file; this one simply is not on the candidate list. The two roads the note hands over each lead somewhere: -l:calc.a steps around the name assembly, and changing the name back to libcalc.a puts it on the list again.

The third scenario is the most ill-used of all: the library is lying right in our current directory, and we wrote not one -L:

```text
$ gcc main.o -lcalc -o app_bare
/usr/bin/ld: cannot find -lcalc: No such file or directory
collect2: error: ld returned 1 exit status
```

libcalc.a and libcalc.so are both there in the mixed directory, but the linker's directory list admits only what -L adds plus that built-in string of defaults; the directory we are standing in cannot squeeze its way in. If you want it in, writing one -L. is enough. When you someday face a cannot find and investigate, the directions to check are just three: does the name follow the convention? Is the directory on the list? Does the library stand after the references?

## Run Time: A Different Party Does the Finding

Remember the shadow round? What link time picked was the fake libz in the -L directory; what run time loaded in was the real libz from /usr/lib. The question part 08 left behind can be answered in full right here: why, when link time went smoothly, does run time still need LD_LIBRARY_PATH standing by? Because the two stages never had the same party doing the finding. Link time is represented by the linker: it knows -L and its own defaults, and once the linking is done, the -L information lands nowhere in the executable file. Run time is represented by the dynamic linker — the true identity of the interpreter at the tail of part 01's file output, the same party that read NEEDED line by line in part 08. It never saw our link command from beginning to end, so of course it does not know -L. And the LD_LIBRARY_PATH=. we kept writing in part 09 — you can now say plainly what it does: it patches the current directory in as one rank of the environment variable.

The ranks it recognizes, we test one rank at a time. SONAME we met in part 08: the official name a library registers for itself. The three libraries laid ready this time differ in content, but all register the SONAME libcalc.so.1; they sit in three separate directories, with answer returning 10, 20, and 30. The program carries one run-time search path pre-embedded, done with the -rpath linker switch: the linker writes the path into the dynamic section, and there it is as RUNPATH. At link time we write the path inside rpath_dir directly, so as not to drag the link-time question of who gets picked back on stage; NEEDED registers only the SONAME, and which file the link used, run time no longer remembers:

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

(I trimmed the long paths to /tmp/…; the rest is as it came out.) The three results we take one at a time: run bare, what loads in is the return-10 version in the rpath_dir that RUNPATH points to; set LD_LIBRARY_PATH, and the return-20 one presses RUNPATH back down; bring up LD_PRELOAD last, and even the search is spared — the preloaded library is mapped into the process ahead of everyone, its registered SONAME is also libcalc.so.1, the NEEDED line has its owner on the spot, and the whole search that would have followed never happens. We counted with LD_DEBUG=libs: the trace holds not one find library=libcalc line, and the count came out 0.

The complete order we need not memorize; the LD_DEBUG=libs search trace labels each rank's name at the end of its line. The program's two dependencies each got a search; we sieved them with the grep pattern twice over, and the lines left standing we read from the top:

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

(The digit string at the head of each line is the process ID; yours will not come out the same.) Reading downward: libcalc hit on the LD_LIBRARY_PATH rank alone, so its search stops at the second line; libc came up empty on that same rank, next came RUNPATH's turn, and the rank after that reads search cache=/etc/ld.so.cache — the cache's roster happens to carry libc, and one step lands on /usr/lib/libc.so.6. Look also at the subdirectories wedged into the paths, glibc-hwcaps/x86-64-v3 and the like: that is the scheme, since glibc 2.33, for picking an optimized build by the CPU's instruction-set tier; this piece has no use for it — recognizing the directory itself is enough. And if the cache comes up empty too? The last rank is the system's default directories, whose original form we look at in a moment.

This order — LD_LIBRARY_PATH pressing down RUNPATH — older references often write the other way around. Their authors are usually not wrong; what changed generations is the tag. The dynamic section carries two tags for writing run-time search paths: the old-style one is named DT_RPATH, the new-style one DT_RUNPATH. The linker today produces the new style by default, and that default is held by the --enable-new-dtags switch; add --disable-new-dtags and it falls back to the old style. The difference as it lands in the search order is one line: RPATH stands ahead of LD_LIBRARY_PATH, RUNPATH stands behind it. Let's relink the same program with the old-style tag:

```text
$ gcc main.o ./rpath_dir/libcalc.so.1 -Wl,--disable-new-dtags,-rpath,/tmp/…/rpath_dir -o app_oldrpath
$ readelf -d app_oldrpath | grep -E "PATH"
 0x000000000000000f (RPATH)              Library rpath: [/tmp/…/rpath_dir]
$ LD_LIBRARY_PATH=/tmp/…/lp_dir ./app_oldrpath
answer=10
```

Same environment variable, same two libraries, and this time 10 wins: RPATH pressed LD_LIBRARY_PATH back down, the exact reverse of the RUNPATH round. The day you see opposite experimental results on two machines, the difference is most likely this one tag — one look at readelf -d and you know.

## Plainly There, Yet Reported Missing

Part 08 left behind a situation still harder to swallow: the file is plainly there, and it still says not found. Let's set the scene out: this time we embedded no RUNPATH in the program, the library lies in the rpath_dir directory right beside it, and the link used exactly that one:

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

Count through the trace yourself: we embedded no RUNPATH, we set no LD_LIBRARY_PATH, the cache holds no shadow of it either, and the search last reached the system search path rank — the system's default directories, which the documentation calls the trusted directories — and everything we met along /usr/lib was (no such file). And the current directory? No rank has a place for it; its situation is one and the same as link-time scenario three: over there, without a -L it cannot get onto the list; over here there is no -L at all, still less anyone to carry it along. If we want a library like this found, three roads are open. The first is to move the file into a trusted directory — root privileges are unavoidable, and once it is moved you still have to bring ldconfig out: it is the glibc family's cache-keeping tool, which scans the trusted directories and the ones registered in /etc/ld.so.conf.d and writes the name-to-path mapping into /etc/ld.so.cache. The second road is LD_LIBRARY_PATH or RUNPATH, patching the directory into one of the search ranks. The third is writing the path into RUNPATH at release time. My WSL has no passwordless elevation, so I did not actually do that step; let's take a read-only look at what the cache looks like:

```text
$ ldconfig -p | grep "libz\.so$"
	libz.so (libc6,x86-64) => /usr/lib/libz.so
```

That the cache rank really gets used, the previous section's libc trace already played out: after the search cache line it drops straight to trying file=/usr/lib/libc.so.6, one step, one hit. The not-found round is that same line followed by (no such file), with the trusted directories holding up the rear empty too, and only then does the error touch ground. The link-time cannot find and the run-time cannot open shared object file — this piece has now shown you both: one error's letterhead reads /usr/bin/ld, the other's is the program's own name. Which party is speaking? Match the letterheads and they sort themselves out.

## The Three-Piece Set: Three Names, Each Governing Its Own Stretch

With the order told, how the three-piece set part 08 left us gets arranged, we also settle today. The real body's name carries the full version number; the one we built is libcalc.so.1.2.3, and the trade jargon for it is realname. The second name is the official one, libcalc.so.1, registered with the soname switch, with a symlink fitted alongside. The third name serves development linking; its jargon is the linker name, and it is the libcalc.so symlink — exactly the thing part 06's -l assembles:

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

Walk one link from head to end, and the stretch each of the three names governs, we take in one by one: -lcalc assembles the linker name libcalc.so; the linker follows the symlink down to the real body, reads out the SONAME it registered, and what gets written into NEEDED is libcalc.so.1; at run time the dynamic linker searches by that name, hits the symlink libcalc.so.1, and the symlink points back at the real body — that is how the 42 got in. At upgrade time you swap the real body and repoint the symlinks, and not one byte of the executable has to move; that benefit part 08 spoke of, laid onto files, is exactly this arrangement. What each of the version number's three segments records, and which kind of change should move which segment — those matters we hand to part 12 (ABI Design).

Since what NEEDED records is the SONAME, when the SONAME changes, does NEEDED follow? We registered the soname as libcalc.so.9 and relinked a copy, the real body's file name left as it was:

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

NEEDED really did follow, and it honors the SONAME alone; what the real body's file happens to be called, NEEDED does not care about. With no file named libcalc.so.9 in the directory, the run-time search still comes up empty; patch in one same-named symlink and it lives again. And when the SONAME is left unset? The behavior splits in two, and we tested both. A library found through -l gets its file name recorded in NEEDED — part 08's hand-built libmyutil.so registered exactly libmyutil.so, and this is where that came from. A library linked by its full path gets, of all things, the whole slash-carrying path recorded:

```text
$ gcc -shared -fPIC -o soname/libcalc_noson.so.5 calc42.c
$ gcc main.o ./soname/libcalc_noson.so.5 -o app_noson
$ readelf -d app_noson | grep calc
 0x0000000000000001 (NEEDED)             Shared library: [./soname/libcalc_noson.so.5]
```

A dependency name carrying a slash drags one more run-time behavior out into the open: seeing the slash in the name, the dynamic linker does not search at all — it takes the name as a literal path relative to the directory the program started from and opens it directly. We ran it from other directories; however long you stretch LD_LIBRARY_PATH, it cannot be brought back. That is why a library released in earnest always registers a SONAME and fixes the name as a bare one without a slash: only then does the searching machinery get something it can take hold of.

## Windows Faces the Same Problem

Everything in this piece has been the ELF way of doing things; on the Windows side the large structure is the same while the details everywhere differ — of the comparison promised in part 06, today we compare one small stretch only. In the link-time search, the MSVC family looks for .lib: the static library's file is one, and the import library fitted to a DLL is one too — a single suffix carrying two identities. The directories searched are supplied by the LIB environment variable, and the name assembly has no lib-prefix affair: foo's library is simply called foo.lib. At run time the loader looks for DLLs: the first place searched is the EXE's own directory, the system directories line up after, and PATH pads out the very end. One spot is exactly reversed between the two, so hold them side by side: on our side, a library lying beside the program gets no attention — the cannot open round in the previous section just played that out — while on the Windows end it runs the other way: putting the DLL and the EXE together is the first-class proper road, the loader searching from the very front by its own nature. That the two stages, linking and running, each do their own searching — that large structure is common to both sides; the closer comparison we hand to part 12 (ABI Design).

## The CMake Perspective

The chores of hand-tuning -L and hand-writing -rpath all belong to CMake in everyday builds. For that string of run-time paths, CMake gives us a dedicated place to write them:

```cmake
add_library(calc SHARED calc_c.c)
add_executable(app main_c.c)
target_link_libraries(app PRIVATE calc)
set_target_properties(app PROPERTIES BUILD_RPATH "/opt/calc_runtime")
```

BUILD_RPATH governs the run-time search path of build-tree artifacts; offhand we wrote into it a /opt/calc_runtime that does not exist yet. Bring --verbose along to look at the link line, and there it really is, landed in the link line's rpath switch:

```text
$ cmake --build build --verbose 2>&1 | grep "cc .*-o app"
/usr/sbin/cc … CMakeFiles/app.dir/main_c.c.o -o app  -Wl,-rpath,/opt/calc_runtime:/tmp/…/build libcalc.so
```

(The middle stretch of the link line and the long paths I trimmed as usual into … and /tmp/…; the rest is as it came out.) Two paths sit in the link line: the /opt/calc_runtime we wrote at the head, and the build directory CMake itself patched in behind. The RUNPATH that part 08 mentioned only as something CMake wrote in along the way, without the why — today the why gets paid back: seeing that the target being linked lives in the build tree, CMake writes the build directory in as well, so an app in the build tree, run bare, finds the libraries of its own tree. There is also the global CMAKE_BUILD_RPATH for adding it all at once, used the very same way.

And the -L end? Part 06's CMake section showed us: for a library born in the same build, CMake hands libmyutil.a's path straight to the link command, carrying -L and the name assembly on our behalf. If the library lives outside the project, the one that steps out to find it is find_library, and what it finds will be an absolute path:

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

## Next Up: Same-Named Symbols, Who Gets the Final Say

Both lines run smooth by now. On the link-time line, the linker finds its libraries with the -L directory list and the name-assembly convention, registering what should be registered and extracting what should be extracted; on the run-time line, the dynamic linker, in the order of preload, environment variable, RUNPATH, cache, trusted directories, gathers the libraries in and maps them in too. But with the libraries all inside the process, the work is only half done: when main calls answer, several export tables in the process stand open — who stands at the head of the line? By what right does the stand-in win? How do we guard our own symbols? And how do we narrow the opening on purpose? Part 09's preload experiment showed one corner of it; part 11 (Symbol Visibility) is the piece that takes this one matter head on.
