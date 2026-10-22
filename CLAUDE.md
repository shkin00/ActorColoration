# CLAUDE.md

Guidance for Claude Code in this repository.

## What this is

`ActorColoration` is an Unreal Engine 5 editor plugin. The repository root is the plugin root: users clone it into
`<Project>/Plugins/ActorColoration`. Target engine: UE 5.8.

Machine-specific paths (engine, test project, build commands) live in `CLAUDE.local.md`, which is gitignored.

## Layout

- `ActorColoration.uplugin` - the descriptor. One module, `ActorColoration`, of `"Type": "Editor"`, so it never builds
  into packaged games.
- `Source/ActorColoration/` - module code. Implementation goes in `Private/`; add `Public/` only for API that other
  modules use.

## Build

- In a host project: build the project's editor target; the plugin builds with it. Output goes to this repo's
  `Binaries/` and `Intermediate/` (gitignored).
- Standalone check: `RunUAT BuildPlugin -Plugin=<repo>/ActorColoration.uplugin -Package=<output dir>`. It builds the
  plugin in a generated host project and catches dependencies or includes that a real host project hides.

## Conventions

- This is a public repository. Test data comes from a private project: never commit its paths, names, assets, logs or
  screenshots.
- `.gitattributes` has `* text=auto` (LF in the repository). Source files have no UTF-8 BOM.
- English for code, comments, docs and commit messages.
