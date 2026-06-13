# Tutorial_AwesomeModernCPP

[中文](README.md) | English

> A practice-oriented modern C++ learning project: from C/C++ fundamentals and modern language features to concurrency, performance, engineering, embedded practice, and open-source code study.

<p align="center">
  <a href="https://awesome-embedded-learning-studio.github.io/Tutorial_AwesomeModernCPP/en/">
    <img src="https://img.shields.io/badge/📖_Click_Me_Ahead_For_Read_Docs_Online-Live-blue?style=for-the-badge" alt="Online Docs">
  </a>
</p>

![C++](https://img.shields.io/badge/C%2B%2B-11%20%7C%2014%20%7C%2017%20%7C%2020%20%7C%2023-blue?logo=c%2B%2B)
![Release](https://img.shields.io/github/v/release/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP)
![Tag](https://img.shields.io/github/v/tag/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP?sort=semver&label=tag)
![License](https://img.shields.io/github/license/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP)
![Build](https://img.shields.io/github/actions/workflow/status/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/deploy.yml?branch=main)

---

## What This Project Is

<p align="center"><em>A systematic modern C++ tutorial — from syntax to silicon, bringing modern C++ to the desktop, STM32 embedded, and industrial open-source projects.</em></p>

10 volumes, 350+ articles, from C/C++ fundamentals through concurrency, performance, engineering, and domain practice — every key concept backed by a CMake example verified in CI, not an unbuildable snippet stranded in an article.

<p align="center">
  <img src="https://img.shields.io/badge/articles-350%2B-blue" alt="articles">
  <img src="https://img.shields.io/badge/C%2B%2B-11%20%7C%2014%20%7C%2017%20%7C%2020%20%7C%2023-009688" alt="C++ standard">
  <img src="https://img.shields.io/badge/embedded-STM32%20F1-FFC107" alt="embedded">
  <img src="https://img.shields.io/badge/examples-CMake%20%7C%20CI%20verified-3F51B5" alt="examples">
</p>

**Who is it for?** New to C/C++ · C or embedded background · Already know C++, want engineering depth

## Highlights

<table>
  <tr>
    <td width="50%" align="center"><h4>🔧 From syntax to silicon</h4>Go beyond desktop C++ — hands-on STM32F1 embedded: register access, interrupt safety, zero-overhead abstraction, cross-compilation & linker scripts.</td>
    <td width="50%" align="center"><h4>⚡ Real, runnable examples</h4>CMake projects validated in CI — not unbuildable snippets stranded in articles.</td>
  </tr>
  <tr>
    <td align="center"><h4>📚 One complete path</h4>10 volumes, 350+ articles — fundamentals → modern features → standard library → advanced → concurrency → performance → engineering → domains.</td>
    <td align="center"><h4>🚀 C++23 current</h4>Covers and practices concepts, coroutines, ranges and more — not stuck at C++11.</td>
  </tr>
  <tr>
    <td align="center"><h4>🔍 Read real code, real talks</h4>Vol.9 studies Chromium (e.g. OnceCallback); Vol.10 is reading notes on CppCon and other talks.</td>
    <td align="center"><h4>🌐 Engineered + bilingual</h4>VitePress (search / dark mode / GitHub Pages auto-deploy) + Chinese main line + English translation + C++98→23 reference cards.</td>
  </tr>
</table>

## Start Here

The fastest path is to read the online docs:

- [Online documentation](https://awesome-embedded-learning-studio.github.io/Tutorial_AwesomeModernCPP/en/)
- [C++ feature reference cards](https://awesome-embedded-learning-studio.github.io/Tutorial_AwesomeModernCPP/en/cpp-reference/)
- [Embedded development track](https://awesome-embedded-learning-studio.github.io/Tutorial_AwesomeModernCPP/en/vol8-domains/embedded/)
- [Community articles](https://awesome-embedded-learning-studio.github.io/Tutorial_AwesomeModernCPP/en/community/)

Run the docs site locally:

```bash
git clone https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP.git
cd Tutorial_AwesomeModernCPP

pnpm install
pnpm dev
# Visit http://localhost:5173/Tutorial_AwesomeModernCPP/
```

Production build and preview:

```bash
BUILD_CONCURRENCY=8 pnpm build
pnpm preview
# Visit http://localhost:4173/Tutorial_AwesomeModernCPP/
```

## Content Guide

The visual roadmap (ten-volume content map + learning paths by background) is integrated into the "Project Roadmap" section on the online docs home page:

→ [View the visual roadmap online](https://awesome-embedded-learning-studio.github.io/Tutorial_AwesomeModernCPP/en/#roadmap)

> 📋 For volume content and progress see the [project roadmap](todo/000-project-roadmap.md); for release history see [changelogs/](changelogs/).

## Local Development and Checks

<details>
<summary>Common commands</summary>

| Command / Script | Purpose |
|------------------|---------|
| `pnpm dev` | Start the VitePress dev server with hot reload |
| `pnpm build` | Production build with per-volume parallel build and search-index merge |
| `pnpm build:single` | Run the regular single VitePress build |
| `pnpm check:links` | Check internal Markdown and component links |
| `pnpm preview` | Preview the production build |
| `pnpm hooks:install` / `scripts/setup_precommit.sh` | Install pre-commit checks |
| `pnpm coverage` | Show English translation coverage |
| `pnpm coverage:update` | Update the English coverage badge in `README.md` |
| `.venv/bin/python scripts/validate_frontmatter.py` | Validate article frontmatter |
| `.venv/bin/python scripts/check_quality.py documents/` | Run content quality checks |
| `.venv/bin/python scripts/build_examples.py --host` | Build host-side CMake examples |
| `.venv/bin/python scripts/build_examples.py --stm32` | Build STM32 example projects |

</details>

<details>
<summary>Project structure, releases, and branches</summary>

**Project Structure**

- `documents/` — 10 tutorial volumes (bilingual), plus community / cpp-reference / compilation / projects
- `code/` — code examples, STM32F1 projects, and reusable templates
- `site/` — VitePress configuration, theme, and plugins
- `scripts/` — build, check, coverage, and content tooling
- `todo/`, `changelogs/` — content roadmap and release history

> For the full directory and navigation, see the [online docs](https://awesome-embedded-learning-studio.github.io/Tutorial_AwesomeModernCPP/) sidebar.

**Version History**

See [changelogs/](changelogs/) for full release history.

**Branch Overview**

| Branch | Purpose | Status |
|--------|---------|--------|
| `main` | Primary development branch | Active |
| `archive/legacy_20260415` | Pre-restructuring archive | Read-only |
| `gh-pages` | Auto-deployed documentation site | Auto-generated |

</details>

## Contributing

Contributions are welcome: documentation fixes, example improvements, new chapters, translation review, issue reports, content suggestions, or submissions to [Community Articles](https://awesome-embedded-learning-studio.github.io/Tutorial_AwesomeModernCPP/en/community/). Please read [CONTRIBUTING.md](./CONTRIBUTING.md) first.

Quick workflow: Fork --> feature branch --> commit --> push --> pull request

If you have questions, feel free to open an issue at [GitHub Issues](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/issues).

## Contributors

Thanks to everyone who has contributed to this project! See [CONTRIBUTORS.md](./CONTRIBUTORS.md) for details.

> Contributions are not limited to code. UI design, illustrations, issue reports, and content suggestions all count. See [CONTRIBUTING.md](./CONTRIBUTING.md).

## Acknowledgements

This project references the following excellent resources:

- [modern-cpp-tutorial](https://github.com/changkun/modern-cpp-tutorial)
- [CPlusPlusThings](https://github.com/Light-City/CPlusPlusThings)
- [CppCon](https://www.youtube.com/user/CppCon)
- [C++ Reference](https://en.cppreference.com/)

## License & Contact

- **License**: [MIT License](./LICENSE)
- **Issues**: [Submit an issue](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/issues)
- **Email**: <725610365@qq.com>
- **Organization**: [Awesome-Embedded-Learning-Studio](https://github.com/Awesome-Embedded-Learning-Studio)
