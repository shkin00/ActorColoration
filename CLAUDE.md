# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

`ActorColoration` is an Unreal Engine 5 editor plugin that adds actor coloration modes to the level editor viewport
(View Mode > Actor Coloration): each mode colors every visible primitive by one of its properties. The idea comes from
Hauke Thiessen's "Visualizing everything in Unreal's editor viewport"
(https://haukethiessen.com/visualizing-everything-in-unreals-editor-viewport/).

The repository root is the plugin root: users clone it into `<Project>/Plugins/ActorColoration`. Target engine: UE 5.8.

Machine-specific paths (engine, test project, build commands) live in `CLAUDE.local.md`, which is gitignored.

## How it works

- Each mode is a handler registered with the engine's `FActorPrimitiveColorHandler` (`Engine` module,
  `GameFramework/ActorPrimitiveColorHandler.h`); no engine changes. A handler is an `FName`, display text, tooltip and a
  `TFunction<FLinearColor(const UPrimitiveComponent*)>` called for each primitive.
- Handlers are registered in the module's `StartupModule` and unregistered by name in `ShutdownModule`. The View Mode
  menu lists registered handlers each time it is built, so no editor subsystem is needed.
- A handler's color is computed when the mode is activated and whenever a primitive's scene proxy is created; it is not
  re-evaluated every frame.
- Level editor viewports switch modes only through View Mode > Actor Coloration. The console command
  `SHOW ACTORCOLORATION <HANDLERNAME>` is handled by `UGameViewportClient`, so it applies to game viewports (PIE).

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
  `-StrictIncludes` disables PCHs and unity builds to check that every header is self-contained;
  `-TargetPlatforms=Win64` skips the other platforms the engine has registered.
- BuildPlugin deletes the contents of the `-Package` directory before building, and refuses a directory that contains
  the plugin or lies inside the engine directory. Use an empty scratch directory.

## Verification

There are no automated tests. A build is checked by its success (it writes
`Binaries/Win64/UnrealEditor-ActorColoration.dll`) and by `Mounting Project plugin ActorColoration` in the editor log.
A coloration mode is checked in the level editor viewport, through View Mode > Actor Coloration.

## Conventions

- This is a public repository. Test data comes from a private project: never commit its paths, names, assets, logs or
  screenshots.
- `.gitattributes` has `* text=auto` (LF in the repository). Source files have no UTF-8 BOM.
- Tabs for indentation in source files and the `.uplugin`. Markdown is wrapped at 120 columns.
- English for code, comments, docs and commit messages.
