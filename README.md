# ActorColoration

Unreal Engine 5 editor plugin.

## Install

Clone into your project's `Plugins` folder, then rebuild the editor (the plugin ships as source):

```
git clone https://github.com/shkin00/ActorColoration.git <Project>/Plugins/ActorColoration
```

Built and tested with Unreal Engine 5.8.

### Perforce or SVN projects

To share the plugin with a team whose project is in Perforce or SVN, keep this git clone as the source and copy
snapshots into the project with `Tools/Update-VendoredCopy.ps1`:

```
.\Tools\Update-VendoredCopy.ps1 -Vcs Perforce -Target <Project>\Plugins\ActorColoration
.\Tools\Update-VendoredCopy.ps1 -Vcs Svn -Target <Project>\Plugins\ActorColoration
```

It copies the files committed at `-Revision` (default `HEAD`) and opens the differences: a pending changelist named
`ActorColoration <commit>` in Perforce, scheduled adds and deletes in SVN. Nothing is submitted unless you pass
`-Submit`. `Binaries` and `Intermediate` in the target are left alone. Run `Get-Help .\Tools\Update-VendoredCopy.ps1
-Full` for details.

## License

MIT. See [LICENSE](LICENSE).
