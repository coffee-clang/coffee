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

We follow the example of [curl](https://curl.se) and [Sqlite](https://sqlite.org/). Most of the rules are taken or
inspired by them.

We deviate from curl on indentation: we use tabs (tab width 4) with
continuation indent 4 and column limit 120. Curl uses 2-space indent
and 79-column limit.

After each change, follow the instructions in the README.md. When those instructions conflict with AGENTS.md or the
command specific documentation, ask for additional instructions.

## Command Documentation

When planning or implementing any subcommand, read the corresponding markdown file in `docs/commands/` for detailed documentation about that command's purpose, usage, and implementation notes.

## Coding style

The coding style is defined in the file CONTRIBUTING.md

## Compilation

The default is to produce a static binary that incorporates all dependencies and has all debug symbols. The compilation
must be optimized for speed, we don't care about binary size.

## Testing

We write as many tests as we can. We run all the static code analyzer tools we can on the code – frequently. We run
fuzzers on the code non-stop.

## Readability

Code should be easy to read. It should be clear. No hiding code under clever constructs, fancy macros or overloading. Easy-to-read code is easy to review, easy to debug and easy to extend.

Smaller functions are easier to read and understand than longer ones, thus preferable.

Code should read as if it was written by a single human. There should be a consistent and uniform code style all over,
as that helps us read code better. Wrong or inconsistent code style is a bug. We fix all bugs we find.

## Naming

Try using a non-confusing naming scheme for your new functions and variable names. It does not necessarily have to mean that you should use the same as in other places of the code, just that the names should be logical, understandable and be named according to what they are used for. File-local functions should be made static. We like lower case names.

## Braces

In if/while/do/for expressions, we write the open brace on the same line as the keyword and we then set the closing
brace on the same indentation level as the initial keyword.

Never omit the braces.

For functions the opening brace should be on a separate line:

## space before parentheses

When writing expressions using if/while/do/for, there shall be a space between the keyword and the open parenthesis.

## Use boolean conditions

Use a test agains `nullptr` instead of a pointer against NULL or 0.
This means that

`if (ptr)` is not good style

`if (ptr != nullptr)` is good style

## No assignments in conditions

To increase readability and reduce complexity of conditionals, we avoid assigning variables within if/while conditions. We frown upon this style:

```
if((ptr = malloc(100)) == NULL)
  return NULL;
```

and instead we encourage the above version to be spelled out more clearly:

```
ptr = malloc(100);
if(ptr == nullptr)
  return NULL;
```

## New block on a new line

We never write multiple statements on the same source line, even for short if() conditions.

## Space around operators

Please use spaces on both sides of operators in C expressions. Postfix (), [], ->, ., ++, -- and Unary +, -, !, ~, &
operators excluded they should have no space.

## No parentheses for return values

We use the 'return' statement without extra parentheses around the value:

```int works(void)
{
  return TRUE;
}
```

## Parentheses for sizeof arguments

When using the sizeof operator in code, we prefer it to be written with parentheses around its argument:

```
int size = sizeof(int);
```

## Column alignment

Some statements cannot be completed on a single line because the line would be too long, the statement too hard to read, or due to other style guidelines above. In such a case the statement spans multiple lines.

If a continuation line is part of an expression or sub-expression then you should align on the appropriate column so that it is easy to tell what part of the statement it is. Operators should not start continuation lines. In other cases follow the 4-space continuation indent (one tab level).

## No typedefed structs

Use structs by all means, but do not typedef them. Use the struct name way of identifying them:

```struct something {
   void *valid;
   size_t way_to_write;
};
struct something instance;
```

Not okay:

```typedef struct {
   void *wrong;
   size_t way_to_write;
} something;
something instance;
```

## Copy strings

Use `memccpy` to copy a string. Avoid using `memcpy`.

## Banned functions

To avoid footguns and unintended consequences we forbid the use of a number of C functions.

This is the full list of functions generally banned.

```
_access
_fstati64
_lseeki64
_mbscat
_mbsncat
_open
_tcscat
_tcsdup
_tcsncat
_tcsncpy
_waccess
_wcscat
_wcsdup
_wcsncat
_wfopen
_wfreopen
_wopen
accept
accept4
access
aprintf
atoi
atol
calloc
close
CreateFile
CreateFileA
CreateFileW
fclose
fdopen
fopen
fprintf
free
freeaddrinfo
freopen
fstat
getaddrinfo
gets
gmtime
llseek
LoadLibrary
LoadLibraryA
LoadLibraryEx
LoadLibraryExA
LoadLibraryExW
LoadLibraryW
localtime
lseek
malloc
mbstowcs
MoveFileEx
MoveFileExA
MoveFileExW
msnprintf
mvsnprintf
open
printf
realloc
recv
rename
send
snprintf
socket
socketpair
sprintf
sscanf
stat
strcat
strcpy
strdup
strerror
strncat
strncpy
strtok
strtok_r
strtol
strtoul
vaprintf
vfprintf
vprintf
vsnprintf
vsprintf
wcscpy
wcsdup
wcsncpy
wcstombs
WSASocket
WSASocketA
WSASocketW
```

Follow the indication of `make tidy`, the program must be clean of warnings.

### Configuration or convention

All parameters must be obtained from the `Coffee.toml` file.

## Types and objects

- binary data are stored as array of `unsigned char`
- use https://github.com/antirez/sds for managing strings
