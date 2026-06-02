# locate-project

Print the absolute path to the project's manifest file.

## Description

Locates `Coffee.toml` by walking up the directory tree and prints its absolute path as JSON.

## Usage

```
coffee locate-project
```

## Implementation Notes

- Outputs: `{ "root": "/absolute/path/to/Coffee.toml" }`
- Walks parent directories until a Coffee.toml is found
