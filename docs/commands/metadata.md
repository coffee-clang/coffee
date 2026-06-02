# metadata

Output resolved package metadata in JSON format.

## Description

Prints the resolved dependency tree, features, and transitive dependencies of the current project as JSON.

## Usage

```
coffee metadata
```

## Implementation Notes

- Outputs to stdout in JSON format
- Includes package name, version, edition, description, license
- Resolves features and outputs feature sets per package
- Walks dependencies recursively up to depth 3 for transitive deps
