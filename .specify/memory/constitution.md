<!-- Sync Impact Report:
- Version change: (initial) → 1.0.0
- Modified principles: 
  - [PRINCIPLE_1_NAME] → I. Library-First
  - [PRINCIPLE_2_NAME] → II. CLI Interface
  - [PRINCIPLE_3_NAME] → III. Test-First (NON-NEGOTIABLE)
  - [PRINCIPLE_4_NAME] → IV. Observability
  - [PRINCIPLE_5_NAME] → V. Simplicity and Performance
- Added sections: Coding Standards, Development Workflow, Governance
- Removed sections: None (all template sections utilized)
- Templates requiring updates:
  - ⚠ .specify/templates/plan-template.md (Constitution Check section needs specific principles)
  - ⚠ .specify/templates/spec-template.md (review for principle alignment)
  - ⚠ .specify/templates/tasks-template.md (review for principle alignment)
- Follow-up TODOs: None
-->
# Coffee Constitution

## Core Principles

### I. Library-First
Every feature starts as a standalone library. Libraries must be self-contained, independently testable, and documented. Each library must have a clear purpose - no organizational-only libraries are permitted.
Rationale: Ensures modularity, reusability, and maintainability by enforcing clear boundaries between components.

### II. CLI Interface
Every library exposes functionality via a command-line interface. Text in/out protocol follows stdin/args → stdout, errors → stderr. Libraries must support both JSON and human-readable output formats.
Rationale: Provides consistent, scriptable interfaces that align with Coffee's nature as a package manager CLI tool.

### III. Test-First (NON-NEGOTIABLE)
Test-Driven Development is mandatory: Tests must be written and approved by users before implementation begins. Tests must initially fail, then implementation makes them pass. The Red-Green-Refactor cycle is strictly enforced.
Rationale: Ensures code quality, correctness, and prevents regressions by validating requirements before implementation.

### IV. Observability
All libraries must implement structured logging for debuggability. Text I/O ensures clear visibility into program flow. Error conditions must be reported to stderr with sufficient context for diagnosis.
Rationale: Enables effective troubleshooting and monitoring of the package manager and its dependencies.

### V. Simplicity and Performance
Start with the simplest solution that works (YAGNI principles). Compilation must be optimized for speed rather than binary size. Avoid premature optimization and unnecessary complexity.
Rationale: Promotes maintainable code and ensures Coffee remains fast and responsive as a command-line tool.

## Coding Standards
Code must adhere to strict formatting and style rules enforced by clang-format and clang-tidy. Key requirements include:
- Braces: Opening brace on same line as keyword (except functions, which get a separate line)
- Space between keywords and opening parentheses in control structures
- Use boolean conditions directly rather than comparing to TRUE/FALSE or NULL
- No assignments in conditional statements
- Multiple statements never on same line
- Space around operators (except postfix/unary operators)
- Return statements without extra parentheses
- sizeof arguments must have parentheses
- No typedefed structs; use struct tag notation
Rationale: Ensures consistent, readable code that follows established C best practices and reduces cognitive overhead.

## Development Workflow
We follow the examples of curl and SQLite. Key practices include:
- Writing comprehensive tests and running static analysis tools frequently
- Running fuzzers on code continuously
- Code should be clear and easy to read - no hiding logic in clever constructs
- Prefer smaller, focused functions over long ones
- Consistent, uniform code style across the codebase
- Logical, understandable names for functions and variables (lowercase preferred)
- File-local functions must be static
Rationale: Creates a maintainable, reliable codebase that follows proven practices from successful projects.

## Governance
This constitution supersedes all other project practices and documentation. Amendments require:
1. Documentation of proposed changes
2. Review and approval following standard contribution processes
3. Migration plan for existing code if needed
All contributors must verify compliance with this constitution during code reviews. Complexity in implementation must be justified with clear benefits. Refer to AGENTS.md for detailed development guidance.
Rationale: Establishes clear authority for project standards and provides a framework for evolution while maintaining stability.

**Version**: 1.0.0 | **Ratified**: 2026-03-19 | **Last Amended**: 2026-03-19