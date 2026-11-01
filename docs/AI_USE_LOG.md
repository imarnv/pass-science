# AI-use log

AI assistance was used throughout this project and is disclosed here, as the brief requires.

## Tools used

| Tool | Used for | Period |
|---|---|---|
| AI coding assistant (agentic, cloud session with shell, compiler and headless browser) | Architecture, C++ and JavaScript code, tests, packaging, documentation drafts | 31 Oct – 1 Nov 2026 |

## Representative interactions (prompts quoted from the session)

### 1. Project brief and interface direction
- **Prompt:** *"this is the assignment … the ui should not be like vibe coded and like professional website like apple as reference"* (with the assignment PDF attached)
- **Output:** a plan for a C++17 core, a local HTTP transport and a web UI in an Edge app window; then the core library, tests and server.
- **My direction / edits:** required a restrained, professional UI; asked for effort to follow the rubric weights (*"look at the marks distribution in the pdf … accordingly do it"*); required commits under my own name.
- **Verified by:** 46 unit tests; reviewing every view in screenshots.

### 2. FEA solver and verification
- **Prompt:** implement Task C as specified (two-node elements, both Dirichlet BCs, midpoint error via FE interpolation, N = 10/20/40/80).
- **Output:** `src/core/fea/thermal_rod.*`, mesh study, CSV export, plots.
- **Verified by:** hand calculation for N = 2 (T₁ = 185 °C); midpoint error equal to q h²/(8k) with observed order 2.000; my handwritten derivation for Task F checked against the C++ values.

### 3. Repository and delivery
- **Prompt:** *"where are u commiting.. its not showing here"* (with a GitHub screenshot)
- **Output:** found that the commits were local only; pushed them to `main`.
- **Prompt:** *"what exactly do i have to submit in the gform as it has a file selector only"*
- **Output:** the ZIP contents required by §8 of the brief, and which items only I could provide (handwritten pages, effort, sign-off).
- **Verified by:** checking the GitHub branch page; extracting the final ZIP and running `Launch.exe` and `tools/wb_tests.exe` from it (under Wine, since I had no Windows PC available).

## Incorrect or unsuitable suggestions identified

Facts from the development history that you can verify and cite:

| # | Suggestion / output | Why it was wrong | How it was caught | Fix (commit) |
|---|---|---|---|---|
| 1 | Hard-coded expected values in the first backprop unit tests (loss written as 1.18254381, ∂J/∂w as −1.20945, ∂J/∂v as −0.71068224) | Arithmetic errors in the constants; the correct values are 1.18254182, −1.20946448 and −0.71068205 | `wb_tests` failed against the C++ implementation and against an independent Python evaluation of tanh(0.5) | test constants corrected before the first commit |
| 2 | Rod-solver form read `form.elements["length"]` | `HTMLFormCollection.length` is the number of controls, so the "Length L" input was never read and Solve threw `Cannot read properties of undefined (reading 'trim')` | Headless-browser walkthrough timed out on the Thermal view; the console showed the error | f7ef477 |
| 3 | Benchmark created a new worker pool inside the timed region | Thread start-up and allocator warm-up were timed, so the 1-worker pool appeared 50 % slower than the serial loop | Implausible result in the Performance view (T(pool, p = 1) > T(serial)) | f7ef477 |
| 4 | Post-build copy of web assets | Only ran when the executable relinked, so edited JavaScript was not served | UI behaviour did not change after a fix | f7ef477 |
| 5 | File-size test sent files with curl's default `application/x-www-form-urlencoded` type | cpp-httplib limits url-encoded bodies to 8 KB, so every test file returned HTTP 413 although the app (which sends `text/plain`) was fine | All sizes returned 413, including 400 KB | test method corrected; documented in FILE_FORMATS.md |

## Independent verification performed

- 46 unit tests: analytic FEA solution, hand calculation, malformed-file fixtures, serial/parallel agreement, finite-difference gradient check.
- Windows build executed under Wine; full UI walkthrough captured from `Launch.exe`.
- Backpropagation derived by hand (`docs/handwritten/`), and every value matched against `results/backprop_gradients.csv`.
- No physical Windows PC was available. `Launch.exe` was verified under Wine only, and this is disclosed in the README limitations.
