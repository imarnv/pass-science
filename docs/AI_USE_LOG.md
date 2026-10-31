# AI-use log

> **To be completed by the candidate before submission.** The brief requires:
> - tool and model names (where available);
> - representative prompts and the relevant outputs;
> - your edits;
> - how you validated the results;
> - at least one incorrect or unsuitable suggestion you identified (or, if none occurred, a deliberate challenge test and its outcome).
>
> Do not invent interactions: copy them from your actual chat history. Disclosed AI use is explicitly not misconduct under the brief.

## Tools used

| Tool / product | Model (if shown) | Used for | Period |
|---|---|---|---|
| | | | |

## Representative interactions

Copy prompts verbatim from your history. Summarise long outputs and link the resulting commit.

### 1. [Topic, e.g. project architecture]
- **Prompt:**
- **Output (summary):**
- **What I changed / rejected:**
- **How I verified it:**
- **Commit(s):**

### 2. [Topic, e.g. FEA assembly and boundary conditions]
- **Prompt:**
- **Output (summary):**
- **What I changed / rejected:**
- **How I verified it:** for example, the hand calculation for N = 2 (T₁ = 185 °C), and the midpoint error compared with q h²/(8k).
- **Commit(s):**

### 3. [Topic, e.g. interface design / packaging]
- **Prompt:**
- **Output (summary):**
- **What I changed / rejected:**
- **How I verified it:**
- **Commit(s):**

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
- [Add your own checks, e.g. a run on your own Windows PC, re-deriving the backprop by hand.]
