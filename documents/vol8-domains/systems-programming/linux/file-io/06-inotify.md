---
title: "inotify 文件监控:把文件系统的动静变成事件流"
description: "程序怎么知道文件系统发生了什么:inotify 把事件变成 fd 上可读的字节流。本篇实测一次 open(O_CREAT) 挤出 IN_CREATE 与 IN_OPEN 两条、mkdir 的 mask 是 0x40000100(IN_ISDIR 是叠在 mask 里的状态位不是事件)、rename 的 FROM/TO 靠 cookie 配对(实测 8596)、sizeof(inotify_event)=16 而 len 向 16 对齐;watch 不递归(touch 孙目录 0 个事件)、绑 inode 不绑路径(文件改名报 IN_MOVE_SELF 继续跟、原路径换新 inode 后文件级 watch 全程沉默、旧 inode unlink 是 IN_ATTRIB、IN_DELETE_SELF、IN_IGNORED 三连);递归监控要动态补挂加整棵兜底 walk(mkdir -p 的孙目录在事件到手前早已存在)、目录整棵改名后后代 watch 静默而 wd 表路径已过期;合并的真条件是四元组 wd/mask/cookie/name 全同且相邻(读者跟上 10 写 10 条零合并、同文件背靠背 200 写折成 1 条而 4 文件轮流 200 写 200 条全在,IN_MODIFY 当不了写计数器),16384 深度的队列被 587 万次写灌爆后只读回 16385 条、wd=-1 的 IN_Q_OVERFLOW 垫在队尾而丢掉的部分没有任何记录;inotify fd 与 timerfd、pipe 同挂一个水平触发 epoll 单循环调度;同设备跨目录 mv 两侧 cookie 同值配对,跨设备 rename(2) 直接 EXDEV(18)、mv 退化为复制加删除后 cookie 全程 0、事件流里不存在这次搬移;cookie 的身份实测是内核全局计数器"
chapter: 8
order: 6
platform: host
difficulty: intermediate
cpp_standard: [20]
reading_time_minutes: 25
prerequisites:
  - "POSIX 文件 I/O:open/read/write 与 fd 的一生"
  - "std::filesystem:目录与元数据"
related:
  - "OS 资源的 RAII 范式:fd、HANDLE 与映射的同一副骨架"
  - "错误处理范式:从 errno 到 expected"
  - "epoll:Linux I/O 多路复用,从 poll 的瓶颈到兴趣表与就绪队列"
tags:
  - host
  - cpp-modern
  - intermediate
  - 系统编程
  - POSIX
  - 实战
current_status:
  title: 正在打磨
  detail: 本批刚写完,正在通读打磨,表述与实测口径可能随时调整
  variant: polishing
---

# inotify 文件监控:把文件系统的动静变成事件流

[L01](./01-posix-file-io.md) 开头交代边界的时候留过一句预告:文件锁和 inotify,放到本卷后续的章节再说。文件锁那一篇已经收了尾,这一篇就轮到 inotify 了。咱们在前五篇里对文件系统做的都是主动去问:open 与 read 问的是内容,mmap 问的是字节,fsync 问的是数据到了盘没有,readdir 问的是名单,文件锁问的是归属。这一篇咱们把方向整个倒过来:文件系统那一侧自己发生了什么,咱们的程序要怎么知道?

最省事的路子是轮询,咱们每隔几秒把目录 stat 上一遍,拿新旧两份名单的快照做差。可是一旦目录深了,轮询的间隔就变成了延迟,间隔一压短了,CPU 也就白烧了。Linux 的答法是把通知做成 fd:inotify(名字是 inode notify 的缩写,它在 2.6.13 的内核里进了主线)发给咱们一只新的 fd,被登记过的地方一有动静,内核就把事件一条条写进它的缓冲,咱们 read 出来的,就是结构化好的字节流。您在 [L01](./01-posix-file-io.md) 里练过的那套 fd 手艺,read 的循环、dup2 的重定向,在它身上是原样能用的,“一切皆文件描述符”的老话,在文件监控上又兑现了一次。

事件流能干的事情相当实在:守护进程靠它守着配置,配置变了就重载。构建工具的增量编译靠它触发,等源码目录动了,该重编的就有了名单。同步器靠它守住了工作目录,新的改动一出来就走。桌面搜索的索引,也是这么维护的。本篇的六个实验沿一条线走:事件长的是什么样(E1),watch 的挂法(E2),递归的搭法(E3),队列的合并与溢出是什么脾气(E4),epoll 的接法(E5),跨目录与跨设备的搬移咱们认不认得出(E6)。

环境的口径照例交代清楚,后面的数字都要拿它对表:实验出自笔者的台机,CPU 用的是 AMD Ryzen 7 9700X,系统是 WSL2 的环境,内核是 6.18.33.2-microsoft-standard-WSL2 的构建,g++ 用的是 16.2.1,编译的口径一律 `-std=c++20 -O2 -Wall -Wextra -Wpedantic`,拿到的是零警告。数据写在 `/home/charliechen/l06_scratch/` 下面的 e1 到 e6(E6 的跨设备一侧另用 `/dev/shm`),复跑之前您得把 `~/l06_scratch` 建出来。实验的编号是 E1 到 E6,与仓库 `code/volumn_codes/vol8/systems-programming/linux/file-io/06-inotify/` 下的 01 到 06 六个目录一一对应,代码与全部原始输出都收进了存档,您随时能对表。[L03](./03-page-cache.md)、[L04](./04-filesystem.md) 与 [L05](./05-file-lock.md) 也各自用着大写 E 系的编号,那些指的是各篇自己的实验,咱们这里的 E 只认本篇,您翻存档的时候认目录号就好。老三件 `unique_fd`、`sys_call`、`errno_code` 沿用 [RAII 篇](../../thinking/01-raii-paradigm.md)与[错误处理篇](../../thinking/02-error-paradigm.md)的定义,咱们只引用。实验另有三件 inotify 专属的小工具(sysctl 上限的读取、mask 位解码、事件流读取器),都放在实验目录的 `common/article.hpp` 里,正文中咱们随用随认。

## 一只 fd、一张 watch 表、一条事件流

```c
int inotify_init(void);                                             /* 拿事件流的 fd */
int inotify_add_watch(int fd, const char *pathname, uint32_t mask); /* 登记监视项 */
int inotify_rm_watch(int fd, int wd);                               /* 摘除监视项 */
```

咱们三步就能把它跑起来。`inotify_init()` 返回一只新的 fd,它就是后面所有事件的出水口。想要非阻塞或者 close-on-exec 的话,咱们改用 `inotify_init1(flags)`,`IN_NONBLOCK` 与 `IN_CLOEXEC` 的含义和 open 的同名位一样,本篇实验的 fd 一律带了 `IN_NONBLOCK`。`inotify_add_watch()` 登记的是一条路径,登记的对象可以是目录,也可以是单个的文件,mask 是您想订阅的事件位集,返回的 wd(watch descriptor,监视项的编号)是它在实例里的身份证,事件流里报的就是它。同一个实例对同一个 inode 再次 add 的话,内核改的是既有 watch 的 mask,交回来的 wd 还是原来那个,所以拿 wd 当 map 的键是安全的。摘除的活交给 `inotify_rm_watch()`,不过 E1 里咱们会亲眼看到,有一类 watch 轮不到咱们亲手摘。

事件长出来的样子,内核也是用一个结构体交代给咱们:

```c
struct inotify_event {
    int      wd;      /* 事件属于哪只 watch */
    uint32_t mask;    /* 事件位;状态位 IN_ISDIR 也叠在这里 */
    uint32_t cookie;  /* 同一次 rename 的两条事件用它配对,其余恒 0 */
    uint32_t len;     /* name 占的字节数,含结尾 '\0',向 sizeof 对齐 */
    char     name[];  /* 被操作的条目名;watch 对象是自己时无名字 */
};
```

x86-64 上的 `sizeof(struct inotify_event)` 是 16:四个整型字段把它排满了,name 是不占位的柔性数组。`len` 的算法容易看走眼,它数的东西,是 name 含结尾空字节的全部字节,再垫到结构体大小的整数倍,man 7 inotify 写的也是这两步。咱们拿 `f.txt` 来算,5 个字节加上结尾的 1 个,凑成了 6,也就垫到了 16。存档的完整输出里还有一个 32 字符的长名字,加 1 之后凑成了 33,垫到了 48。`name` 只在 watch 的是目录、被操作的是里面的条目时出现,watch 的若是文件自己,len 的值就恒为 0,E2 的输出会给实证。

read 的行为也交代清楚:一次成功的 read 交回的是整数条事件,缓冲区里装着一条或多条完整的结构体。缓冲区要是连最大的一条事件都装不下,read 就直接回您一个 EINVAL,所以咱们的读取缓冲给 4096 绰绰有余,毕竟最长的名字要受 NAME_MAX(单个名字的字节上限,Linux 上是 255)的限制,撑死了也就 16+256 字节的量。队列空的时候,阻塞版的 read 会睡过去,咱们带了 `IN_NONBLOCK`,空队列换来的就是 EAGAIN,咱们正好当“没了”的信号用。读取器的骨架长这样:

```cpp
// common/article.hpp(节选):把当前排队的事件一次取干净
std::vector<decoded_event> drain(int fd)
{
    std::vector<decoded_event> out;
    for (;;) {
        char buf[4096];
        ssize_t n = ::read(fd, buf, sizeof buf);            // 一次 read = 整数条事件
        if (n == -1 && errno == EINTR) {
            continue;
        }
        if (n == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            break;                                          // 队列空了,非阻塞版的“没了”
        }
        if (n == -1) {
            throw std::system_error{errno_code(), "read(inotify fd)"};
        }
        decode_append(buf, static_cast<std::size_t>(n), out);
        if (static_cast<std::size_t>(n) < sizeof buf) {
            break;                                          // 短读即读干
        }
    }
    return out;
}
```

解码的半段咱们用 `memcpy` 把定长头部拷出来,而不拿 `char` 数组直接做指针转型,缓冲的对齐不归咱们管,完整的实现都在存档里。

inotify 自己的额度,内核摆在了 `/proc/sys/fs/inotify/` 下面,咱们实验的程序每次起跑都会打一遍:

| 旋钮 | 本机值 | 管的事 |
|---|---|---|
| `max_user_watches` | 524288 | 每个真实用户全部实例合计能挂的 watch 数 |
| `max_user_instances` | 1024 | 每个真实用户能 init 出的实例(fd)数 |
| `max_queued_events` | 16384 | 单个实例的事件队列深度,超了报 IN_Q_OVERFLOW |

前两个管的是资源额度,咱们在 E3 会拿 7 只 watch 对一下占比。第三个管的是队列深度,E4 的 D 组会把它灌爆。

> inotify 看得见的,只有本机进程经由文件系统 API 触发的动静,NFS 这类网络文件系统在远端发生的事,咱们是收不到的,man 7 inotify 的原话就是请您退回轮询。它还有个更大只的亲戚叫 fanotify(7),管的是全文件系统级的监控,要的权限也高,杀毒与审计那一档的工具用的就是它,咱们应用内的目录监控用 inotify 就对了。

## E1 事件流长什么样:一次 open 挤出两条,状态位叠在 mask 里

E1 的走法是这样:咱们把 watch 挂在测试目录 tree 上,mask 给的是 `IN_ALL_EVENTS`,子进程照着十步的脚本逐步制造事件,父进程的命令经管道发过去,子进程做完了就回一个 ack(应答字节),这时咱们才去读事件。同步咱们不走 sleep,走的是管道握手,原因是事件的入队发生在写方 syscall 返回之前,ack 到达的时候,事件必定已经在队列里了。咱们等十步跑完,收到的事件流长这样(完整的输出在存档里,做了删节):

```text
$ ./e1
watched: /home/charliechen/l06_scratch/e1/tree  mask: IN_ALL_EVENTS
/proc/sys/fs/inotify/: max_user_watches=524288 max_user_instances=1024 max_queued_events=16384
sizeof(struct inotify_event) = 16
inotify_add_watch("/home/charliechen/l06_scratch/e1/tree", IN_ALL_EVENTS) = wd 1

==== 第  1 步:open(f.txt, O_WRONLY|O_CREAT|O_TRUNC) ====
  wd=1   mask=0x00000100 IN_CREATE                                    cookie=0      len=16  name='f.txt'
  wd=1   mask=0x00000020 IN_OPEN                                      cookie=0      len=16  name='f.txt'

==== 第  2 步:write(fd, "hello", 5) ====
  wd=1   mask=0x00000002 IN_MODIFY                                    cookie=0      len=16  name='f.txt'

==== 第  3 步:close(fd) ====
  wd=1   mask=0x00000008 IN_CLOSE_WRITE                               cookie=0      len=16  name='f.txt'

==== 第  4 步:open O_RDONLY + read 2 字节 + close ====
  wd=1   mask=0x00000020 IN_OPEN                                      cookie=0      len=16  name='f.txt'
  wd=1   mask=0x00000001 IN_ACCESS                                    cookie=0      len=16  name='f.txt'
  wd=1   mask=0x00000010 IN_CLOSE_NOWRITE                             cookie=0      len=16  name='f.txt'

...(第 5 步 chmod 报 IN_ATTRIB、第 7 步 unlink 报 IN_DELETE、第 9 步 rmdir 报 IN_DELETE|IN_ISDIR,略)...

==== 第  6 步:rename(f.txt, g.txt)  同目录改名 ====
  wd=1   mask=0x00000040 IN_MOVED_FROM                                cookie=8596   len=16  name='f.txt'
  wd=1   mask=0x00000080 IN_MOVED_TO                                  cookie=8596   len=16  name='g.txt'

==== 第  8 步:mkdir(sub) ====
  wd=1   mask=0x40000100 IN_CREATE|IN_ISDIR                           cookie=0      len=16  name='sub'

==== 第 10 步:rmdir(tree)  被监控目录本身被删 ====
  wd=1   mask=0x00000400 IN_DELETE_SELF                               cookie=0      len=0   name=''
  wd=1   mask=0x00008000 IN_IGNORED                                   cookie=0      len=0   name=''

目录没了之后再 inotify_rm_watch(wd=1) = -1, errno = 22 (Invalid argument)
```

咱们一处一处看。头一步就够咱们看一阵:只调了一句 `open(O_CREAT)`,队列里却进了两条,IN_CREATE 说的是“目录里多了个孩子”,IN_OPEN 说的是“这个文件被打开了”。事件的粒度是动作而不是系统调用,所以您别拿事件数去数 syscall 的次数。close 也分家:写打开的收尾报 IN_CLOSE_WRITE,只读打开的收尾报 IN_CLOSE_NOWRITE,分界线到了 E6 还有大用。

第 6 步是 rename 的标准照,咱们看两条:IN_MOVED_FROM 报的是 f.txt,IN_MOVED_TO 报的是 g.txt,两条的 cookie 都是 8596。cookie 平时的值恒为 0,非零的场合只有搬移对,内核拿它当同一次搬移的配对暗号,man 的措辞是“连接相关事件的整数”。第 8 步的 mask 是 0x40000100,咱们把它按位摊开:IN_CREATE(0x100),上面叠的是 IN_ISDIR(0x40000000)。IN_ISDIR 说的不是动作而是对象,这次动的是目录。它的作用只是叠在事件位上,声明一下对象的身份,所以同一个 rmdir 拿到的是 IN_DELETE|IN_ISDIR,等咱们到 E3 里给目录 deep 整棵改名,还会看到 IN_MOVED_FROM 叠着 IN_ISDIR 的样子。判事件类型的时候,咱们把 IN_ISDIR 从 mask 里滤掉,剩下的才是动作。

第 10 步把镜头对准了被监控的目录自己。tree 被删的那一刻,事件流里进来的是 IN_DELETE_SELF 加 IN_IGNORED 两条,两条的 len 都是 0、name 都是空,因为当事的就是它自己,也就不需要报名了。IN_IGNORED 的语义,man 写的是 watch 被显式移除,或者随对象的消亡被自动移除。也就是说内核已经替咱们把 watch 摘掉了,咱们后面再补一句 `inotify_rm_watch`,回给咱们的就是 -1 加 EINVAL。您要是拿 wd 建了表,收到 IN_IGNORED 就该同步地摘表,E3 的代码就是这么干的。

常用的事件位,咱们一并收进一张表,数值都跟实测的输出对过:

| 事件位 | 值 | 含义 |
|---|---|---|
| `IN_ACCESS` | 0x00000001 | 文件被读 |
| `IN_MODIFY` | 0x00000002 | 文件被写 |
| `IN_ATTRIB` | 0x00000004 | 元数据变了(权限、时间戳、链接数) |
| `IN_CLOSE_WRITE` | 0x00000008 | 写打开的关闭 |
| `IN_CLOSE_NOWRITE` | 0x00000010 | 只读打开的关闭 |
| `IN_OPEN` | 0x00000020 | 文件被打开 |
| `IN_MOVED_FROM` | 0x00000040 | 移出,配 cookie |
| `IN_MOVED_TO` | 0x00000080 | 移入,配 cookie |
| `IN_CREATE` | 0x00000100 | 目录里创建了条目 |
| `IN_DELETE` | 0x00000200 | 目录里删除了条目 |
| `IN_DELETE_SELF` | 0x00000400 | 被 watch 的对象自己被删 |
| `IN_MOVE_SELF` | 0x00000800 | 被 watch 的对象自己被移 |
| `IN_Q_OVERFLOW` | 0x00004000 | 队列溢出,wd=-1 |
| `IN_IGNORED` | 0x00008000 | watch 被摘除,显式或自动 |
| `IN_ISDIR` | 0x40000000 | 状态位:对象是目录 |

## E2 watch 挂在哪儿:不递归,绑的是 inode

E2 的对象是两层目录 A/B 加若干文件,咱们分步挂 watch、分步做动作。头一个观察冲着的,就是咱们的路径直觉:咱们把 watch 只挂在 A 上,去 touch 一个孙辈的文件 A/B/y:

```text
$ ./e2  (节选)
/proc/sys/fs/inotify/: max_user_watches=524288 max_user_instances=1024 max_queued_events=16384
wd_a=1 → A/(目录)

==== 第 3 步:touch A/B/y(watch 只挂在 A 上) ====
  (0 个事件)
```

咱们拿到的答案就是 0 个事件。咱们的 watch 挂在 A 上,动的却是 A 的孙辈 A/B/y,内核一个字都没报给咱们。inotify 的 watch 只覆盖被登记的那一层目录,子目录是沾不上光的,不递归是它的语义,而不是实现上的缺陷。咱们想看孙辈的动静,就得把 watch 挂到 B 的头上。补挂之后的同一个动作,咱们再看一遍:

```text
wd_b=2 → A/B/(目录) —— 现在子目录也有自己的表了
==== 第 3 步:touch A/B/y(watch 只挂在 A 上) ====
  wd=2   mask=0x00000020 IN_OPEN                                      cookie=0      len=16  name='y'
  wd=2   mask=0x00000008 IN_CLOSE_WRITE                               cookie=0      len=16  name='y'
```

A 上的 wd_a 依旧沉默,报告的是 B 上的 wd_b。事件的归属按被 watch 的目录算,而不是按路径前缀算,咱们递归监控的地基就在这里,E3 会给每个目录都配上自己的 watch。

咱们还试了两个边角。咱们给 `add_watch` 带上 `IN_ONLYDIR`,再拿它去挂普通文件的话,返回的是 -1 加 ENOTDIR(errno 20),它的角色是“我只要目录”的防呆位,想挂目录又怕路径中途被换成文件的时候用得上。反过来给单个的文件挂 watch,事件是照样报的,只是 name 的取值恒为空,len 的值也是 0,道理咱们在 E1 讲过:目录 watch 的 name 报的是被操作的条目,文件 watch 报的就是它自己的动静。

后面的三步合起来,能把 watch 绑的是什么交代清楚。man 7 inotify 的原话是 `Inotify monitoring is inode-based`,咱们拿实验给它配上三个证据。

改名的现场是头一个证据。咱们把 A/f rename 成 A/f.bak,目录侧的 wd_a 报了 IN_MOVED_FROM 与 IN_MOVED_TO(cookie 8619 配对),文件自己的 wd_f 报的则是 IN_MOVE_SELF:watch 跟着 inode 走了,换了名字也没撒手。

```text
==== 第 7 步:rename A/f → A/f.bak ====
  wd=1   mask=0x00000040 IN_MOVED_FROM                                cookie=8619   len=16  name='f'
  wd=1   mask=0x00000080 IN_MOVED_TO                                  cookie=8619   len=16  name='f.bak'
  wd=3   mask=0x00000800 IN_MOVE_SELF                                 cookie=0      len=0   name=''
```

咱们接着看第二个证据:原路径换人的现场。咱们在原来的 A/f 路径上放了一个新文件(新 inode)并写入,目录侧的 wd_a 把 CREATE、OPEN、MODIFY、CLOSE_WRITE 全报了一遍,而文件侧的 wd_f 全程沉默,一条都没给咱们:它绑着的旧 inode 还活着,只是不再住在这个路径上了,新 inode 与它已经无关了。这就是绑 inode 的另一面,靶子换了,监控端是未必知道的。

收尾给了咱们第三个证据。旧 inode 所在的 A/f.bak 被 unlink,wd_f 收到的是三连:IN_ATTRIB(unlink 改的是链接数,归在元数据的事件里)、IN_DELETE_SELF、IN_IGNORED,随后咱们再对它调 `inotify_rm_watch`,回给咱们的同样是 EINVAL。

```text
==== 第 9 步:unlink A/f.bak(被 watch 的旧 inode 被删) ====
  wd=3   mask=0x00000004 IN_ATTRIB                                    cookie=0      len=0   name=''
  wd=3   mask=0x00000400 IN_DELETE_SELF                               cookie=0      len=0   name=''
  wd=3   mask=0x00008000 IN_IGNORED                                   cookie=0      len=0   name=''
  wd=1   mask=0x00000200 IN_DELETE                                    cookie=0      len=16  name='f.bak'
```

这三步合起来的工程含义,咱们要单独停一下。原子替换配置文件的标准姿势,恰恰就是写一个临时文件再 rename 顶上去的招,日志的滚动走的也是同一条路,而它正是“原路径换上新 inode”。只给具体文件挂 watch 的程序,在文件被顶替的那一刻就换到了旧 inode 上,新文件的动静一条都收不到。咱们想跟住“这个路径上现在是谁”,watch 就该挂在目录的身上,名字的认定交给事件流,搬移的配对再交给 cookie。

## E3 递归监控:补挂要整棵,wd 表要自己改

内核既然是不递归的,递归就得咱们自己搭。咱们的搭法是三件活。咱们起手把整棵树 walk 一遍,让每个目录都有自己的 watch。事件里见到 `IN_CREATE|IN_ISDIR` 的话,咱们就给新目录补挂。见到 IN_IGNORED 的话,咱们就把 wd 从表里摘掉。核心的补挂代码不长:

```cpp
// recursive_watch.cpp(节选):对新目录整棵补挂
void add_tree(const fs::path& dir, bool announce)
{
    add_one(dir, announce);            // 起点自己也要挂:迭代器只遍历孩子
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(
             dir, fs::directory_options::skip_permission_denied, ec);
         it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) { break; }
        if (!it->is_directory(ec)) { continue; }
        add_one(it->path(), announce);
    }
}
```

容易想岔的地方在补挂的深度,咱们得留神。事件驱动的思路听起来只要“新目录来了就挂它”,可是 `mkdir -p a/b/c` 是一次性把三层全造好的,等咱们的程序收到 a 的 IN_CREATE,孙目录 a/b/c 早就躺在盘上了,只挂 a 的话,b 与 c 的后续动静就漏了。所以补挂走的不是 add_one 而是 add_tree,从新目录往下做整棵的兜底。实测的输出把这场竞态摆得很清楚:

```text
$ ./e3  (节选)
起手:walk 整棵树,每个目录挂一只 watch
  [watch] add_watch(wd=1) /home/charliechen/l06_scratch/e3/tree
起手 watch 数:1

==== 第 1 步:mkdir -p a/b/c(嵌套树) ====
  [event] wd=1  IN_CREATE|IN_ISDIR  /home/charliechen/l06_scratch/e3/tree/a
  [watch] add_watch(wd=2) /home/charliechen/l06_scratch/e3/tree/a
  [watch] add_watch(wd=3) /home/charliechen/l06_scratch/e3/tree/a/b
  [watch] add_watch(wd=4) /home/charliechen/l06_scratch/e3/tree/a/b/c

==== 第 2 步:touch a/1.txt a/b/2.txt a/b/c/3.txt ====
  [event] wd=2  IN_CREATE  /home/charliechen/l06_scratch/e3/tree/a/1.txt
  [event] wd=3  IN_CREATE  /home/charliechen/l06_scratch/e3/tree/a/b/2.txt
  [event] wd=4  IN_CREATE  /home/charliechen/l06_scratch/e3/tree/a/b/c/3.txt
```

到咱们手里的事件只有一条,a/b 与 a/b/c 的 watch 全靠兜底 walk 补上。咱们随后 touch 三个不同深度的文件,wd=2、wd=3、wd=4 各报各的,兜底 walk 的价值就落在这三行里。

目录整棵改名是另一个反直觉的现场。`mv deep` 改名成 `deep2` 的时候,父目录报了一对 `IN_MOVED_FROM|IN_ISDIR` 与 `IN_MOVED_TO|IN_ISDIR`,deep 自己的 watch(wd=5)报了 IN_MOVE_SELF,而 deep 的后代 deep/x、deep/x/y,路径明明全变了,咱们却一个事件都没等到:内核只给被改名的那个 inode 报 MOVE_SELF,后代是沾不到光的。

```text
==== 第 5 步:mv deep → deep2(整目录改名) ====
  [event] wd=1  IN_MOVED_FROM|IN_ISDIR  /home/charliechen/l06_scratch/e3/tree/deep
  [event] wd=1  IN_MOVED_TO|IN_ISDIR    /home/charliechen/l06_scratch/e3/tree/deep2
  [event] wd=5  IN_MOVE_SELF            /home/charliechen/l06_scratch/e3/tree/deep
```

三只 watch 事后都还是有效的,因为它们绑的都是 inode,可是咱们 wd 表里存的路径已经过期了。真要追路径的 watcher,收到 MOVE_SELF 的时候就得改表,把它名下整棵子树的路径前缀换掉,没有谁能替您改。成本这边也顺手量了一下:整个实验挂了 7 个目录、7 只 watch,占本机 `max_user_watches`(524288)的 0.0013%。一只 watch 对应的是一个内核对象,占用线性地跟着目录数走,编辑器的工程视图、同步器的源目录,常见的量级在几千到几万,离上限的距离还远。不过这个额度是按真实用户全局算的,一台机器上的所有进程一起花。

## E4 队列:合并的真条件,溢出之后没有记录

E4 是本篇分量最重的一组实验。市面上流传的说法是“IN_MODIFY 会被内核合并”,这句话其实传歪了,咱们用四组实验把合并的条件与溢出的下场都量了出来。man 7 inotify 的原文咱们整句摆上来:如果事件流里前后相继的两条事件完全相同,man 写的是 `same wd, mask, cookie, and name`,而且前一条还没被读走,内核就把它们折叠成了一条。前三组对着这句话的不同部位做检验,第四组去把它的队列灌爆。

A 组是读者跟得上的场合。咱们的子进程每写一拍,父进程的读也立刻跟上,咱们走上十个来回:

```text
==== A 读者跟上:每写一拍读一拍 ====
10 次写 → 10 条 IN_MODIFY:读者跟上,一条不少
```

为什么零合并?咱们看 A 组的做法:读进程每次都把队列读空了,新事件进来的时候,队列里没有可供叠放的对象,合并的条件从根上就不成立。这也顺带解释了咱们日常为什么很少遇见合并:肯读的事件驱动程序,队列常年是空的。

B 组是读者不读的场合,咱们对同一个文件背靠背写 200 次:

```text
==== B 读者不读:200 次背靠背写 solo/same.txt ====
200 次写 → 1 条事件:
  wd=1   mask=0x00000002 IN_MODIFY                                    cookie=0      len=0   name=''
```

200 次的写,在事件流里只剩下了孤零零一条。wd、mask、cookie、name 的四元组全同且相邻,内核一路叠了下来。实测的工程含义很硬:IN_MODIFY 当不了写次数的计数器,您想知道文件被写了几次,inotify 的机制里就没有这个数,真要次数的话,得去内核的审计子系统里找,audit 与 auditd 那一套就是干这个的。

“同类都合并”的传讹,咱们用 C 组来治。咱们还是不读,让四个文件轮流地写,凑满 200 次的总量:

```text
==== C 读者不读:轮流写 c0..c3 共 200 次 ====
200 次写 → 200 条事件(首尾各 3):
  wd=2   mask=0x00000002 IN_MODIFY                                    cookie=0      len=16  name='c0.txt'
  wd=2   mask=0x00000002 IN_MODIFY                                    cookie=0      len=16  name='c1.txt'
  wd=2   mask=0x00000002 IN_MODIFY                                    cookie=0      len=16  name='c2.txt'
  ...
  wd=2   mask=0x00000002 IN_MODIFY                                    cookie=0      len=16  name='c1.txt'
  wd=2   mask=0x00000002 IN_MODIFY                                    cookie=0      len=16  name='c2.txt'
  wd=2   mask=0x00000002 IN_MODIFY                                    cookie=0      len=16  name='c3.txt'
```

读回来的是完整的 200 条。咱们单看事件类型的话,四份 IN_MODIFY 是一模一样的,可是名字是轮着来的,队列尾部与来客的四元组永远对不上,咱们数到的折叠是零次。所以准确的表述是咱们实测出的这句:完全相同且相邻的事件会被合并,光类型相同是不算数的,隔了别的事件也不算数。B 与 C 摆在一起的时候,这句话的两个条件就都有了各自的实证。

D 组咱们灌爆队列。咱们起四个写者进程,每人负责的都是 128 个文件,狂写了 1.5 秒,咱们让父进程全程不读,收工之后咱们再一口气读:

```text
==== D 灌爆队列:4 写者 × 128 文件 × 1500 ms,父进程全程不读 ====
max_queued_events = 16384
  [写者 1] 完成写次数 1474816
  [写者 3] 完成写次数 1480192
  [写者 2] 完成写次数 1477120
  [写者 0] 完成写次数 1442560
读出 16385 条事件,其中 IN_Q_OVERFLOW 1 条,首次出现在第 16384 条:
  wd=2   mask=0x00000002 IN_MODIFY                                    cookie=0      len=16  name='w1_0.bin'
  wd=2   mask=0x00000002 IN_MODIFY                                    cookie=0      len=16  name='w0_0.bin'
  ...
  wd=-1  mask=0x00004000 IN_Q_OVERFLOW                                cookie=0      len=0   name=''
```

四个写者合计完成了 5874688 次写,咱们读回来的事件总共 16385 条:前面的 16384 条全是 IN_MODIFY,再加一条 wd=-1 的 IN_Q_OVERFLOW,它恰好垫在队列的最后一个位置上(从 0 数起是第 16384 条)。队列的深度就是 `max_queued_events` 的 16384,队列装满了之后,内核就把此后的事件整个丢了,只在队尾补了这么一条溢出标记。最要紧的是时间线的后半段:队列灌满了之后写者其实还在写,事件流里却再没进过一条了。差出来的五百多万次写,连同被合并进前 16384 条里的那些,都没有留下可核对的记录。

::: warning 收到 IN_Q_OVERFLOW 之后该做什么
溢出条目的 wd 是 -1,与咱们挂的任何 watch 都对不上,丢过这件事它说了,丢的是什么、丢了多少,它是一概不说的。正确的反应是把它当成“事件流已不可信”的信号,立刻对监控的树做一趟全量的重扫,拿现状当新的基线。读不过来是常有的事,应用层的周期性核对因此不能省,它防的就是没被注意到的溢出。要是继续按部就班地处理后续事件,等于拿着一份缺了页的变更清单去更新索引。
:::

读得快是咱们最基本的防御,E5 的 epoll 循环干的就是这个活。

## E5 接进 epoll:三类 fd,一个循环

咱们讲到这里,inotify 身上还有一层没派上用场的性质:它的事件流本身就是一只普通的 fd,理所当然进得了 epoll,“有事件可读”在内核的眼里就是读端就绪,与 pipe 里来了数据、timerfd 到了期没有任何特权差别。咱们在 E5 把三类 fd 挂进了同一个水平触发的 epoll,一边是 inotify 的 fd,一边是每 500 毫秒到期的 timerfd,还有子进程每 300 毫秒写一行的 pipe。

```cpp
// epoll_loop.cpp(节选):三类 fd 同挂一个 epoll,单循环统一调度
auto add = [&](int fd, const char* tag) {
    epoll_event ev{};
    ev.events = EPOLLIN;   // 默认水平触发:只要还有的读,每次 epoll_wait 都报
    ev.data.fd = fd;
    sys_call("epoll_ctl", ::epoll_ctl, ep.get(), EPOLL_CTL_ADD, fd, &ev);
};
add(ifd.get(), "inotify");
add(tfd.get(), "timerfd ");
add(msg_r.get(), "pipe");

while (!bye) {
    epoll_event out[8];
    int n = static_cast<int>(
        sys_call("epoll_wait", ::epoll_wait, ep.get(), out, 8, 5000));
    for (int i = 0; i < n && !bye; ++i) {
        const int fd = out[i].data.fd;     // 三类 fd 混在同一个 events[] 里回来
        // ...按 fd 分发:pipe 读消息,timerfd 读到期数,inotify 读事件...
    }
}
```

```text
$ ./e5  (节选)
epoll_ctl(ADD) fd=3 (inotify), events=EPOLLIN(水平触发)
epoll_ctl(ADD) fd=4 (timerfd ), events=EPOLLIN(水平触发)
epoll_ctl(ADD) fd=5 (pipe     ), events=EPOLLIN(水平触发)

[  302 ms] pipe    :"tick 1-1"
[  502 ms] timerfd :到期 1 次
[  902 ms] pipe    :"tick 1-3"
[ 1002 ms] timerfd :到期 1 次
[ 1102 ms] inotify :wd=1   mask=0x00000100 IN_CREATE         cookie=0      len=16  name='note.txt'
[ 1102 ms] inotify :wd=1   mask=0x00000002 IN_MODIFY         cookie=0      len=16  name='note.txt'
[ 1102 ms] inotify :wd=1   mask=0x00000008 IN_CLOSE_WRITE    cookie=0      len=16  name='note.txt'
...(中间数轮 pipe 与 timerfd 各就各位,略)...
[ 3304 ms] inotify :wd=1   mask=0x00000040 IN_MOVED_FROM     cookie=8809   len=16  name='note.txt'
[ 3304 ms] inotify :wd=1   mask=0x00000080 IN_MOVED_TO       cookie=8809   len=16  name='note2.txt'
[ 3502 ms] timerfd :到期 1 次
[ 3604 ms] pipe    :"BYE"
```

时间线上三类事件是交错着来的,咱们用一个 `epoll_wait` 循环统一调度。1102 毫秒的地方,文件系统的三条动静插在了两轮 tick 的中间。3304 毫秒的地方,rename 的两条与 timerfd 的到期前后脚到,时序上各走各的。epoll 本身的机制课咱们不在这里重开,[网络卷的 epoll 篇](../../../networking/02-epoll-io-multiplexing.md)从 poll 的瓶颈一路讲到兴趣表与就绪队列,您要补课请移步。本卷后面的多路复用篇,咱们还会见到 timerfd 与 eventfd,它们同样把自己化成了 fd,进的是同一套循环。本篇只把衔接点交代在这儿:水平触发的 epoll 配 inotify 是顺手的选择,read 把队列读空了,下一次的 epoll_wait 自然就不报了,连边缘触发里必须循环读到 EAGAIN 的纪律都省了。咱们真想用边缘触发也不难,咱们的 drain 本来就把队列读干,短读与 EAGAIN 都是它的收尾。

## E6 跨目录与跨设备:cookie 的能与不能

搬移是事件流里最难认的动作,咱们把它放到两个场景里各打一遍。场景一是同设备的跨目录搬移:对象文件是 f_samefs,A 与 B 都在 ext4 的地盘,咱们在两边各挂了一只 watch,搬移的命令是 `mv A/f_samefs B/`:

```text
$ ./e6  (节选)
statfs(/home/charliechen/l06_scratch/e6/A) = ext2/3/4 (f_type=0xef53)
statfs(/dev/shm/l06_e6_C               ) = tmpfs (f_type=0x1021994)
wd_a=1 → A/,wd_b=2 → B/,wd_c=3 → C/

==== mv A/f_samefs B/  (ext4 → ext4,rename(2) 一步到位) ====
  wd=1   mask=0x00000040 IN_MOVED_FROM                                cookie=8787   len=16  name='f_samefs'
  wd=2   mask=0x00000080 IN_MOVED_TO                                  cookie=8787   len=16  name='f_samefs'
```

出的记录挂在 wd=1 的名下,进的记录挂在 wd=2 的名下,cookie 把两条连成了同一次搬移,配对是跨着 watch 成立的。man 对这个配对还有一个很坦白的告诫,原话的用词是 inherently racy:FROM 与 TO 的中间可能插着别的事件,两条不保证是相邻的,队列溢出的时候甚至可能只剩一条。所以写配对逻辑的时候,咱们得按“攒着一窗口的 FROM 等着认 TO”的思路来,别指望它们总是肩并肩的。

场景二是跨设备的搬移。A 留在 ext4 的一侧,C 在 `/dev/shm` 的 tmpfs 上,咱们用 statfs(2)(查文件系统类型的调用)也把两家认得清清楚楚。咱们直接调 rename(2):

```text
==== rename(2) 跨设备直接调会怎样 ====
rename(A/f_xdev, C/f_xdev) = -1, errno = 18 (Invalid cross-device link)
  (0 个事件)
```

回给咱们的是 errno 18(EXDEV)。rename(2) 在机制上就是不给跨文件系统的,mv 命令对此是早有准备的,它退回去走的是复制加删除。咱们照 mv 的做法手动演了一遍,事件流是这样的:

```text
==== mv 走的退路:复制 + 删除(自己照 mv 的做法演一遍) ====
  wd=1   mask=0x00000020 IN_OPEN                                      cookie=0      len=16  name='f_xdev'
  wd=3   mask=0x00000100 IN_CREATE                                    cookie=0      len=16  name='f_xdev'
  wd=3   mask=0x00000020 IN_OPEN                                      cookie=0      len=16  name='f_xdev'
  wd=1   mask=0x00000001 IN_ACCESS                                    cookie=0      len=16  name='f_xdev'
  wd=3   mask=0x00000002 IN_MODIFY                                    cookie=0      len=16  name='f_xdev'
  wd=3   mask=0x00000008 IN_CLOSE_WRITE                               cookie=0      len=16  name='f_xdev'
  wd=1   mask=0x00000010 IN_CLOSE_NOWRITE                             cookie=0      len=16  name='f_xdev'
  wd=3   mask=0x00000004 IN_ATTRIB                                    cookie=0      len=16  name='f_xdev'
  wd=1   mask=0x00000200 IN_DELETE                                    cookie=0      len=16  name='f_xdev'
```

您把 cookie 那一列从头扫到尾,看到的全是 0。源侧的样子像“被读了一遍再被删掉”,目的侧的样子像“来了个新文件”,两边说的都是真话,可是这一次搬移的事实,在事件流里是不存在的。跨设备的搬移,监控端只能靠时间窗加同名的启发式去猜,猜错了也无处对证。E1 里讲过的分界,在这里恰好成了线索:源侧的收尾是 CLOSE_NOWRITE,目的侧的收尾是 CLOSE_WRITE,咱们真要写识别逻辑,手里最硬的特征位就是它们了。

cookie 的身份,咱们顺手也验了一下:它是内核里的全局计数器。E1 的 rename 拿到的是 8596,E2 的拿到了 8619,E6 的拿到了 8787,三个不同的程序、三个不同的 inotify 实例,计数却是一路接着往上走的,数的显然是整个内核的事。E5 输出里的 8809 比 E6 的还大,也不是乱了:输出的次序按捕获时间排,E6 那轮程序其实比 E5 更早捕获,跟实验编号是不同步的。所以 cookie 的数值本身别当信息用,它唯一的用途就是在同一条事件流里比相等。配对的活,咱们也只能在自己 fd 的事件流里做:别人家的流里有什么、什么时候到,咱们一无所知,跨进程的配对,cookie 是办不了的。

Linux 线的下一站是内存管理,咱们把进程的地址空间摊开看。

## 另一侧怎么看

Windows 那边是没有 inotify 的,同一件事交给了 `ReadDirectoryChangesW`:咱们拿 `FILE_LIST_DIRECTORY` 的权限打开目录句柄,调 `ReadDirectoryChangesW` 的时候递交一个缓冲,收到的就是 `FILE_NOTIFY_INFORMATION` 格式的记录,动作码是一组 `FILE_ACTION_` 前缀的常量,眼熟的有 ADDED、MODIFIED、REMOVED 这几样,改名的旧名新名也各有各的码。咱们看它跟 inotify 明显不同的地方,一处是传了 `bWatchSubtree=TRUE` 就天生递归,咱们 E3 手搭的那一层它白送。另一处的不同在于通知同样会丢,丢了之后的补救,口径同样是整棵的重扫。异步收结果的活靠重叠 I/O 或完成端口,那正是 Windows 侧异步模型的正门,细说留给本卷后面的异步 I/O 章(还没写),地基您看 [Win32 文件 I/O](../../windows/file-io/01-win32-file-io.md)。

<ReferenceCard title="参考资源">
  <ReferenceItem
    :id="1"
    title="inotify(7)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man7/inotify.7.html"
  />
  <ReferenceItem
    :id="2"
    title="inotify_init(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/inotify_init.2.html"
  />
  <ReferenceItem
    :id="3"
    title="inotify_add_watch(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/inotify_add_watch.2.html"
  />
  <ReferenceItem
    :id="4"
    title="inotify_rm_watch(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/inotify_rm_watch.2.html"
  />
  <ReferenceItem
    :id="5"
    title="read(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/read.2.html"
  />
  <ReferenceItem
    :id="6"
    title="epoll(7)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man7/epoll.7.html"
  />
  <ReferenceItem
    :id="7"
    title="fanotify(7)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man7/fanotify.7.html"
  />
  <ReferenceItem
    :id="8"
    title="rename(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/rename.2.html"
  />
  <ReferenceItem
    :id="9"
    author="Michael Kerrisk"
    title="The Linux Programming Interface"
    publisher="No Starch Press"
    :year="2010"
    url="https://man7.org/tlpi/"
  />
</ReferenceCard>
