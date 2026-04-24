# Coffee
A modern package manager for C

## Project structure

We enforce a canonical structure on each project.

### Directory structure

root
├── Coffee.toml <-- Project manifest
├── .gitignore
├── LICENSE
├── README.md
├── include/ <-- Public API Headers
│ └── my_project/
│ └── main_lib.h
├── src/ # source of the project
└── deps/ # All dependencies
└── dep1/ # Dependency no. 1
└── dep2/ # Dependency no. 2
├── tests/ <-- Unit & Integration Tests
│ ├── test_main_lib.c
│ └── unity/ (or reference to external)
├── docs/ <-- Doxygen/Markdown documentation
├── scripts/ <-- Utility scripts (bash/python)
└── build/ <-- Artifacts (ignored by Git)

The dependencies will be kept in the `deps/` directory. Each library will be in a subdirectory.`

Always be lint-clean, using `clang-tidy`
