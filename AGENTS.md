# AGENTS.md

Behavioral guidelines to reduce common LLM coding mistakes. Merge with project-specific instructions as needed.

**Tradeoff:** These guidelines bias toward caution over speed. For trivial tasks, use judgment.

## 1. Think Before Coding

**Don't assume. Don't hide confusion. Surface tradeoffs.**

Before implementing:
- State your assumptions explicitly. If uncertain, ask.
- If multiple interpretations exist, present them - don't pick silently.
- If a simpler approach exists, say so. Push back when warranted.
- If something is unclear, stop. Name what's confusing. Ask.

## 2. Simplicity First

**Minimum code that solves the problem. Nothing speculative.**

- No features beyond what was asked.
- No abstractions for single-use code.
- No "flexibility" or "configurability" that wasn't requested.
- No error handling for impossible scenarios.
- If you write 200 lines and it could be 50, rewrite it.

Ask yourself: "Would a senior engineer say this is overcomplicated?" If yes, simplify.

## 3. Surgical Changes

**Touch only what you must. Clean up only your own mess.**

When editing existing code:
- Don't "improve" adjacent code, comments, or formatting.
- Don't refactor things that aren't broken.
- Match existing style, even if you'd do it differently.
- If you notice unrelated dead code, mention it - don't delete it.

When your changes create orphans:
- Remove imports/variables/functions that YOUR changes made unused.
- Don't remove pre-existing dead code unless asked.

The test: Every changed line should trace directly to the user's request.

## 4. Goal-Driven Execution

**Define success criteria. Loop until verified.**

Transform tasks into verifiable goals:
- "Add validation" → "Write tests for invalid inputs, then make them pass"
- "Fix the bug" → "Write a test that reproduces it, then make it pass"
- "Refactor X" → "Ensure tests pass before and after"

For multi-step tasks, state a brief plan:
```
1. [Step] → verify: [check]
2. [Step] → verify: [check]
3. [Step] → verify: [check]
```

Strong success criteria let you loop independently. Weak criteria ("make it work") require constant clarification.

---

**These guidelines are working if:** fewer unnecessary changes in diffs, fewer rewrites due to overcomplication, and clarifying questions come before implementation rather than after mistakes.

# Project specific part

After each change, check README.md

# Coding style

Coding style is strictly enforced by calling indent after each change with the following parameters:

`
--align-with-spaces
--no-blank-lines-after-commas
--no-blank-lines-after-declarations
--blank-lines-after-procedures
--no-blank-lines-before-block-comments
--braces-on-func-def-line
--braces-on-if-line
--braces-on-struct-decl-line
--break-before-boolean-operator
--dont-break-function-decl-args
--dont-break-function-decl-args-end
--dont-break-procedure-type
--case-brace-indentation 4
--case-indentation 0
--no-comment-delimiters-on-blank-lines
--comment-indentation 33
--continuation-indentation 4
--continue-at-parentheses
--cuddle-do-while
--cuddle-else
--declaration-comment-column 33
--declaration-indentation 1
--dont-format-first-column-comments
--dont-format-comments
--else-endif-column 33
--honour-newlines
--indent-label 1
--indent-level 8
--dont-left-justify-declarations
--line-comments-indentation 0
--line-length 120
--dont-line-up-parentheses
--no-parameter-indentation
--paren-indentation 2
--preprocessor-indentation 4
--remove-preprocessor-space
--preserve-mtime
--no-space-after-casts
--space-after-for
--no-space-after-function-call-names
--space-after-if
--no-space-after-parentheses
--space-after-while
--spaces-around-initializers
--space-special-semicolon
--dont-star-comments
--struct-brace-indentation 4
--swallow-optional-blank-lines
--dont-tab-align-comments
--tab-size 8
--no-tabs
`

You can use the most recent C standard (C23).


