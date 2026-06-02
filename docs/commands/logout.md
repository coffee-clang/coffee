# logout

Remove registry credentials.

## Description

Removes the stored API token from `~/.coffee/credentials`.

## Usage

```
coffee logout
```

## Implementation Notes

- Deletes the credentials file at `~/.coffee/credentials`
- Reports success or if no credentials were found
