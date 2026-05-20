# Tutorial_AwesomeModernCPP

[中文](README.md) | English

> A systematic modern C++ tutorial -- from foundational syntax to embedded practice, with compilable code examples for every concept

<p align="center">
  <a href="https://awesome-embedded-learning-studio.github.io/Tutorial_AwesomeModernCPP/">
    <img src="https://img.shields.io/badge/📖_Read_Docs_Online-Live-blue?style=for-the-badge" alt="Online Docs">
  </a>
</p>

![C++](https://img.shields.io/badge/C%2B%2B-11%20%7C%2014%20%7C%2017%20%7C%2020%20%7C%2023-blue?logo=c%2B%2B)
![Release](https://img.shields.io/github/v/release/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP)
![License](https://img.shields.io/github/license/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP)
![Build](https://img.shields.io/github/actions/workflow/status/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/deploy.yml?branch=main)

---

## Highlights

- **9-Volume System** -- From C crash course to embedded practice, forming a complete learning loop
- **Compilable Examples** -- Every concept comes with a CMake project, not isolated code snippets
- **Embedded Practice** -- STM32 multi-platform real hardware projects
- **Tag Navigation** -- Browse articles by topic, C++ standard, difficulty, and platform
- **Online Reading** -- Full-featured documentation site with search, navigation, and dark mode

---

## Content Architecture

```mermaid
graph LR
    V1["Vol.1 Fundamentals"] --> V2["Vol.2 Modern Features"]
    V2 --> V3["Vol.3 Std Library"] & V4["Vol.4 Advanced"] & V5["Vol.5 Concurrency"] & V6["Vol.6 Performance"] & V7["Vol.7 Engineering"]
    V2 --> V8["Vol.8 Domain Apps"]
    V8 --> E["Embedded"] & N["Networking"] & G["GUI"] & D["Data"] & A["Algorithms"]
    V2 --> V9["Vol.9 Open Source"]
    V9 --> OC["Chrome Code Study"] & OS["Other Projects"]
```

<details>
<summary>Volume details and progress</summary>

| Volume | Topic | Articles | Difficulty | Status |
|:--:|------|:------:|:----:|:----:|
| 1 | [C++ Fundamentals](documents/vol1-fundamentals/) -- types, control flow, functions, pointers, classes, template basics | 49 | beginner | Completed |
| 2 | [Modern C++ Features](documents/vol2-modern-features/) -- move semantics, smart pointers, constexpr, Lambda | 44 | intermediate | Completed |
| 3 | [Standard Library In Depth](documents/vol3-standard-library/) -- containers, iterators, algorithms, strings, allocators | 40-50 | intermediate | Planned |
| 4 | [Advanced Topics](documents/vol4-advanced/) -- Concepts, Ranges, coroutines, modules, template metaprogramming | 50-60 | advanced | Planned |
| 5 | [Concurrent Programming](documents/vol5-concurrency/) -- thread primitives, atomic operations, lock-free programming, async I/O | 25-30 | advanced | Planned |
| 6 | [Performance Optimization](documents/vol6-performance/) -- CPU cache, SIMD, reading assembly, benchmarking | 18-22 | advanced | Planned |
| 7 | [Software Engineering Practices](documents/vol7-engineering/) -- CMake, testing, static analysis, DevOps | 30-35 | intermediate | Planned |
| 8 | [Domain Applications](documents/vol8-domains/) -- embedded / networking / GUI / data storage / algorithms | 80-100 | intermediate | In Progress |
| 9 | [Open Source Project Study](documents/vol9-open-source-project-learn/) -- reading and analyzing open source codebases | 13+ | intermediate | In Progress |
| - | [Compilation & Linking In Depth](documents/compilation/) -- preprocessing, assembly, linking, debug symbols | 10+ | intermediate | Completed |
| - | [Capstone Projects](documents/projects/) -- hand-rolled STL, mini HTTP server, embedded OS | - | advanced | Planned |

</details>

---

## Learning Paths

```mermaid
flowchart TD
    subgraph PathA["Path A -- C and Embedded Experience"]
        A1["Vol.2: Modern C++ Features"] --> A2["Vol.8: Embedded Development"]
    end
    subgraph PathB["Path B -- C++ Experience"]
        B1["Vol.8: Fundamentals Review"] --> B2["Platform Tutorials"] --> B3["RTOS Practice"]
    end
    subgraph PathC["Path C -- Both"]
        C1["Jump to any topic of interest"]
    end
    subgraph PathD["Path D -- Complete Beginner"]
        D1["Vol.1: C++ Fundamentals (incl. C crash course)"] --> D2["Vol.2: Modern C++ Features"]
    end
    Start(["Your starting point?"]) -->|"C + Embedded"| PathA
    Start -->|"C++ Experience"| PathB
    Start -->|"Both"| PathC
    Start -->|"No experience"| PathD

    style PathA fill:#dbeafe,stroke:#3b82f6,color:#1e3a5f
    style PathB fill:#dcfce7,stroke:#22c55e,color:#14532d
    style PathC fill:#fff7ed,stroke:#f97316,color:#7c2d12
    style PathD fill:#f3e8ff,stroke:#a855f7,color:#581c87
```

---

## Quick Start

```bash
git clone https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP.git
cd Tutorial_AwesomeModernCPP
pnpm install              # Install dependencies
pnpm hooks:install        # Install the pre-commit formatting and coverage hook

# Build and preview (closer to production behavior)
# Set BUILD_CONCURRENCY to your nproc output for faster parallel builds
BUILD_CONCURRENCY=16 pnpm build && pnpm preview
# Visit http://localhost:5173/Tutorial_AwesomeModernCPP/

# Or: start the dev server (with hot-reload) for debugging
pnpm dev
# Visit http://localhost:5173/Tutorial_AwesomeModernCPP/
```

<details>
<summary>More commands and developer tools</summary>

| Command / Script | Purpose |
|-------------|------|
| `pnpm dev` | Start VitePress dev server (hot reload) |
| `pnpm build` | Production build (parallel per-volume build + search index merge) |
| `pnpm build:single` | Single build (no volume splitting) |
| `pnpm preview` | Preview production build |
| `pnpm hooks:install` / `scripts/setup_precommit.sh` | Install the pre-commit Git hook |
| `pnpm coverage` | Show English translation coverage |
| `pnpm coverage:update` | Update the English coverage badge in `README.md` |
| `scripts/validate_frontmatter.py` | Validate article frontmatter |
| `scripts/check_links.py` | Check internal link validity |
| `scripts/analyze_frontmatter.py` | Analyze tutorial statistics |
| `scripts/build_examples.py` | Compile all CMake example projects |
| `scripts/check_quality.py` | Content quality checks |

</details>

---

<details>
<summary>Version history / Branches / Directory structure</summary>

**Version History**

| Version | Date | Notes |
|------|------|------|
| [v0.1.0](changelogs/v0.1.0.md) | 2026-04-29 | Initial public release -- Vol 1/2, compilation, and embedded tutorials |

See [changelogs/](changelogs/) for full release history.

**Branch Overview**

| Branch | Purpose | Status |
|------|------|------|
| `main` | Primary development branch | Active |
| `archive/legacy_20260415` | Pre-restructuring archive | Read-only |
| `gh-pages` | Auto-deployed documentation site | Auto-generated |

**Project Directory Structure**

```text
Tutorial_AwesomeModernCPP/
├── documents/                  # Tutorial Markdown files
│   ├── vol1-fundamentals/      # Volume 1: C++ Fundamentals (ch00-ch12 + C crash course)
│   ├── vol2-modern-features/   # Volume 2: Modern C++ Features
│   ├── vol3-standard-library/  # Volume 3: Standard Library In Depth
│   ├── vol4-advanced/          # Volume 4: Advanced Topics
│   ├── vol5-concurrency/       # Volume 5: Concurrent Programming
│   ├── vol6-performance/       # Volume 6: Performance Optimization
│   ├── vol7-engineering/       # Volume 7: Software Engineering Practices
│   ├── vol8-domains/           # Volume 8: Domain Applications
│   │   ├── embedded/           #   Embedded Development
│   │   ├── networking/         #   Network Programming
│   │   ├── gui-graphics/       #   GUI and Graphics
│   │   ├── data-storage/       #   Data Storage
│   │   └── algorithms/         #   Algorithms and Data Structures
│   ├── vol9-open-source-project-learn/  # Volume 9: Open Source Project Study
│   ├── compilation/            # Compilation & Linking In Depth
│   ├── projects/               # Capstone Projects
│   └── index.md                # Tutorial home page
├── code/                       # Example code
│   ├── volumn_codes/vol1/      #   Volume 1 code and exercises
│   └── examples/               #   Legacy code examples
├── site/                       # VitePress site configuration
│   └── .vitepress/             #   Config, theme, plugins
├── scripts/                    # Developer tool scripts
├── todo/                       # Content planning and progress tracking
└── package.json                # Node.js dependencies and build scripts
```

</details>

---

## Contributing

We welcome contributions of all kinds! Please read [CONTRIBUTING.md](./CONTRIBUTING.md) for details.

Quick workflow: Fork --> Feature branch --> Commit --> Push --> Pull Request

If you have questions, feel free to open an issue at [GitHub Issues](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/issues).

---

## Contributors

Thanks to everyone who has contributed to this project! See [CONTRIBUTORS.md](./CONTRIBUTORS.md) for details.

<!-- ALL_CONTRIBUTORS_START -->
| Contributor | Contributions |
|-------------|--------------|
| [Charliechen](https://github.com/Charliechen114514) | 📝 Content · 🔍 Review · 💡 Examples |
| [Doll-Attire](https://github.com/Doll-Attire) | 🎨 UI Design · 📝 UX Improvements |
<!-- ALL_CONTRIBUTORS_END -->

> Contributions are not limited to code — UI design, illustrations, issue reports, and content suggestions all count. See [CONTRIBUTING.md](./CONTRIBUTING.md).

---

## Acknowledgements

This project references the following excellent resources:

- [modern-cpp-tutorial](https://github.com/changkun/modern-cpp-tutorial)
- [CPlusPlusThings](https://github.com/Light-City/CPlusPlusThings)
- [CppCon](https://www.youtube.com/user/CppCon)
- [C++ Reference](https://en.cppreference.com/)

---

## License & Contact

- **License**: [MIT License](./LICENSE)
- **Issues**: [Submit an issue](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/issues)
- **Email**: <725610365@qq.com>
- **Organization**: [Awesome-Embedded-Learning-Studio](https://github.com/Awesome-Embedded-Learning-Studio)
