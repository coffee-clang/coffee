# Coffee
A modern package manager for C

## Project structure

We enforce a canonical structure on each project.

### Directory structure

root
├── src/       # source of the project
└── deps/      # All dependencies
    └── dep1/      # Dependency no. 1
    └── dep2/      # Dependency no. 2

The dependencies will be kept in the `deps/` directory. Each library will be in a subdirectory.`
