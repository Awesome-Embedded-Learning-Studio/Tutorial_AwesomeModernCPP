# Tutorial_AwesomeModernCPP

[中文](README.md) | English

![C++](https://img.shields.io/badge/C%2B%2B-11%20%7C%2014%20%7C%2017%20%7C%2020%20%7C%2023-blue?logo=c%2B%2B)
![License](https://img.shields.io/github/license/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP)
![Stars](https://img.shields.io/github/stars/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP)
![Issues](https://img.shields.io/github/issues/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP)
![Build](https://img.shields.io/github/actions/workflow/status/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/deploy.yml?branch=main)
[![Docs](https://img.shields.io/badge/docs-online-blue)](https://awesome-embedded-learning-studio.github.io/Tutorial_AwesomeModernCPP/)

> A systematic modern C++ tutorial -- from foundational syntax to embedded practice, from the standard library in depth to concurrent programming, with compilable code examples for every concept

---

## Highlights

- **Systematic Learning Path** -- 8 volumes from beginner to advanced, each with clear prerequisites, building progressively
- **Practice-Driven** -- Every concept comes with a compilable CMake project, not isolated code snippets
- **Multi-Platform Coverage** -- STM32 / ESP32 / RP2040 embedded practice, going beyond desktop
- **Tag Navigation** -- Browse articles by topic, C++ standard, difficulty, platform, and more
- **Online Reading** -- MkDocs documentation site with search and navigation

---

## Content Architecture

```mermaid
graph LR
    V1["Volume 1 Fundamentals"] --> V2["Volume 2 Modern Features"]
    V2 --> V3["Volume 3 Std Library"] & V4["Volume 4 Advanced"] & V5["Volume 5 Concurrency"] & V6["Volume 6 Performance"] & V7["Volume 7 Engineering"]
    V2 --> V8["Volume 8 Domain Apps"]
    V8 --> E["Embedded"] & N["Networking"] & G["GUI"] & D["Data"] & A["Algorithms"]
```

### Tutorial Structure

| Volume | Topic | Articles | Difficulty | Status |
|:--:|------|:------:|:----:|:----:|
| 1 | [C++ Fundamentals](documents/vol1-fundamentals/) -- types, control flow, functions, pointers, classes, template basics | 50-60 | beginner | In Progress |
| 2 | [Modern C++ Features](documents/vol2-modern-features/) -- move semantics, smart pointers, constexpr, Lambda | 35-40 | intermediate | In Progress |
| 3 | [Standard Library In Depth](documents/vol3-standard-library/) -- containers, iterators, algorithms, strings, allocators | 40-50 | intermediate | Planned |
| 4 | [Advanced Topics](documents/vol4-advanced/) -- Concepts, Ranges, coroutines, modules, template metaprogramming | 50-60 | advanced | Planned |
| 5 | [Concurrent Programming](documents/vol5-concurrency/) -- thread primitives, atomic operations, lock-free programming, async I/O | 25-30 | advanced | Planned |
| 6 | [Performance Optimization](documents/vol6-performance/) -- CPU cache, SIMD, reading assembly, benchmarking | 18-22 | advanced | Planned |
| 7 | [Software Engineering Practices](documents/vol7-engineering/) -- CMake, testing, static analysis, DevOps | 30-35 | intermediate | Planned |
| 8 | [Domain Applications](documents/vol8-domains/) -- embedded / networking / GUI / data storage / algorithms | 80-100 | intermediate | In Progress |
| - | [Compilation & Linking In Depth](documents/compilation/) -- preprocessing, assembly, linking, debug symbols | 10+ | intermediate | Completed |
| - | [Capstone Projects](documents/projects/) -- hand-rolled STL, mini HTTP server, embedded OS | - | advanced | Planned |

---

## Learning Paths

```mermaid
flowchart TD
    subgraph PathA["Path A -- C and Embedded Experience"]
        A1["Volume 2: Modern C++ Features"] --> A2["Volume 8: Embedded Development"]
    end
    subgraph PathB["Path B -- C++ Experience"]
        B1["Volume 8: Fundamentals Review"] --> B2["Platform Tutorials"] --> B3["RTOS Practice"]
    end
    subgraph PathC["Path C -- Both"]
        C1["Jump to any topic of interest"]
    end
    Start(["Your starting point?"]) -->|"C + Embedded"| PathA
    Start -->|"C++ Experience"| PathB
    Start -->|"Both"| PathC

    style PathA fill:#dbeafe,stroke:#3b82f6,color:#1e3a5f
    style PathB fill:#dcfce7,stroke:#22c55e,color:#14532d
    style PathC fill:#fff7ed,stroke:#f97316,color:#7c2d12
```

---

## Quick Start

```bash
git clone https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP.git
cd Tutorial_AwesomeModernCPP
bash scripts/mkdoc_setup_local_dependency.sh
bash scripts/local_preview.sh
# Visit http://127.0.0.1:8000
```

<details>
<summary>More developer tools</summary>

| Script | Purpose |
|------|------|
| `mkdoc_setup_local_dependency.sh` | Install MkDocs dependencies |
| `local_preview.sh` | Start local preview server |
| `setup_precommit.sh` | Install pre-commit hooks |
| `validate_frontmatter.py` | Validate article frontmatter |
| `check_links.py` | Check internal link validity |
| `analyze_frontmatter.py` | Analyze tutorial statistics |

</details>

---

## Branch Overview

| Branch | Purpose | Status |
|------|------|------|
| `main` | Primary development branch | Active |
| `archive/legacy_20260415` | Pre-restructuring archive | Read-only |
| `gh-pages` | Auto-deployed documentation site | Auto-generated |

---

<details>
<summary>Project directory structure</summary>

```
Tutorial_AwesomeModernCPP/
├── documents/                  # Tutorial Markdown files
│   ├── vol1-fundamentals/      # Volume 1: C++ Fundamentals
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
│   ├── compilation/            # Compilation & Linking In Depth
│   ├── projects/               # Capstone Projects
│   └── index.md                # Tutorial home page
├── code/                       # Example code
├── scripts/                    # Developer tool scripts
├── todo/                       # Content planning and progress tracking
└── mkdocs.yml                  # MkDocs site configuration
```

</details>

---

## Contributing

We welcome contributions of all kinds! Please read [CONTRIBUTING.md](./CONTRIBUTING.md) for details.

Quick workflow: Fork --> Feature branch --> Commit --> Push --> Pull Request

If you have questions, feel free to open an issue at [GitHub Issues](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/issues).

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
- **Email**: 725610365@qq.com
- **Organization**: [Awesome-Embedded-Learning-Studio](https://github.com/Awesome-Embedded-Learning-Studio)

---

<p align="center">
  <b>Learn modern C++ systematically, from fundamentals to practice</b>
</p>
