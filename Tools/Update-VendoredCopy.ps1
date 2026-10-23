<#
.SYNOPSIS
	Copies a committed revision of the ActorColoration plugin into a Perforce or SVN workspace.

.DESCRIPTION
	The git repository stays the source of truth: the other VCS only receives snapshots, and the copy is never edited
	there. The script exports the files tracked at -Revision with git archive, so ignored local files (CLAUDE.local.md,
	Binaries, Intermediate) are never copied. It then makes -Target match the export and opens the differences in the
	target VCS: changed files, new files, and deletes of files that are no longer in the plugin. Binaries\ and
	Intermediate\ inside -Target are left alone, so a project that checks in plugin binaries keeps them. Empty folders
	are left in place.

	Perforce: the changes go into a new pending changelist described as "ActorColoration <commit>"; an empty changelist
	is deleted. -Submit submits it. The workspace is found from -Target's folder (P4CONFIG) or P4CLIENT.

	SVN: the changes are scheduled in the working copy that contains -Target. -Submit commits -Target with the message
	"ActorColoration <commit>".

	The script refuses a -Target that is a junction or symbolic link, lies inside this repository, or is inside a git
	working tree, so it cannot overwrite the source.

.EXAMPLE
	.\Tools\Update-VendoredCopy.ps1 -Vcs Perforce -Target D:\P4\MyGame\Plugins\ActorColoration

.EXAMPLE
	.\Tools\Update-VendoredCopy.ps1 -Vcs Svn -Target C:\svn\MyGame\Plugins\ActorColoration -Submit
#>
[CmdletBinding()]
param(
	[Parameter(Mandatory = $true)]
	[ValidateSet('Perforce', 'Svn')]
	[string]$Vcs,

	# The plugin folder in the other workspace, normally <Project>\Plugins\ActorColoration. Created if it is missing.
	[Parameter(Mandatory = $true)]
	[string]$Target,

	# The git revision to copy. Only committed content is copied.
	[string]$Revision = 'HEAD',

	# Submit (Perforce) or commit (SVN) the changes instead of leaving them for review.
	[switch]$Submit
)

$ErrorActionPreference = 'Stop'

$PluginName = 'ActorColoration'

# Top-level folders of -Target that are never added, changed or deleted.
$KeptFolders = @('Binaries', 'Intermediate', '.svn')

# Runs a native tool and returns its exit code, stdout lines and stderr lines. Windows PowerShell 5.1 turns redirected
# stderr into terminating errors under $ErrorActionPreference = 'Stop', so the tool runs with 'Continue' and the exit
# code is checked instead.
function Invoke-Tool
{
	param(
		[Parameter(Mandatory = $true)]
		[string]$Exe,
		[string[]]$Arguments = @(),
		[string]$InputText,
		[switch]$AllowFailure
	)

	$ErrorActionPreference = 'Continue'
	if ($PSBoundParameters.ContainsKey('InputText'))
	{
		$Output = $InputText | & $Exe @Arguments 2>&1
	}
	else
	{
		$Output = & $Exe @Arguments 2>&1
	}

	$Result = [PSCustomObject]@{
		ExitCode = $LASTEXITCODE
		Out = @($Output | Where-Object { $_ -isnot [System.Management.Automation.ErrorRecord] } | ForEach-Object { "$_" })
		Err = @($Output | Where-Object { $_ -is [System.Management.Automation.ErrorRecord] } | ForEach-Object { "$_" })
	}
	if ($Result.ExitCode -ne 0 -and -not $AllowFailure)
	{
		throw "'$Exe $($Arguments -join ' ')' failed with exit code $($Result.ExitCode):`n$(($Result.Err + $Result.Out) -join "`n")"
	}
	return $Result
}

function Get-RelativePath([string]$Root, [string]$Path)
{
	$Length = $Root.TrimEnd('\').Length + 1
	if ($Path.Length -le $Length)
	{
		return ''
	}
	return $Path.Substring($Length)
}

function Test-KeptPath([string]$RelativePath)
{
	return $KeptFolders -contains ($RelativePath -split '\\')[0]
}

# Relative paths of the files under $Root, without the kept folders. Junctions and symbolic links to folders are not
# followed, so files behind them are never deleted.
function Get-Files([string]$Root)
{
	$Files = New-Object System.Collections.Generic.List[string]
	if (Test-Path -LiteralPath $Root)
	{
		$Pending = New-Object System.Collections.Generic.Stack[string]
		$Pending.Push($Root)
		while ($Pending.Count -gt 0)
		{
			foreach ($Item in Get-ChildItem -LiteralPath $Pending.Pop() -Force)
			{
				$RelativePath = Get-RelativePath $Root $Item.FullName
				if (Test-KeptPath $RelativePath)
				{
					continue
				}
				if (-not $Item.PSIsContainer)
				{
					$Files.Add($RelativePath)
				}
				elseif (-not ($Item.Attributes -band [IO.FileAttributes]::ReparsePoint))
				{
					$Pending.Push($Item.FullName)
				}
			}
		}
	}
	return ,$Files.ToArray()
}

function Assert-SafeTarget([string]$TargetPath, [string]$RepoRoot)
{
	if (($TargetPath + '\').StartsWith($RepoRoot.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase))
	{
		throw "-Target $TargetPath is inside the source repository $RepoRoot."
	}
	if (Test-Path -LiteralPath $TargetPath)
	{
		$Item = Get-Item -LiteralPath $TargetPath -Force
		if (-not $Item.PSIsContainer)
		{
			throw "-Target $TargetPath is a file, not a folder."
		}
		if ($Item.Attributes -band [IO.FileAttributes]::ReparsePoint)
		{
			throw "-Target $TargetPath is a junction or symbolic link, probably to a git clone. Remove the link first with: cmd /c rmdir `"$TargetPath`" (this deletes only the link, not its target)."
		}
	}
	# A .git folder in -Target or above it also catches a git clone reached through a junction higher up the path.
	$Folder = $TargetPath
	while ($Folder)
	{
		if (Test-Path -LiteralPath (Join-Path $Folder '.git'))
		{
			throw "-Target $TargetPath is inside the git working tree $Folder. Copy into a Perforce or SVN workspace instead."
		}
		$Folder = Split-Path -Parent $Folder
	}
}

function Get-ExistingFolder([string]$Path)
{
	while ($Path -and -not (Test-Path -LiteralPath $Path))
	{
		$Path = Split-Path -Parent $Path
	}
	return $Path
}

# Checked before anything is copied, so a missing workspace leaves -Target untouched. Returns the client and user.
function Get-PerforceWorkspace([string]$TargetPath)
{
	Push-Location -LiteralPath (Get-ExistingFolder $TargetPath)
	try
	{
		$Info = (Invoke-Tool p4 @('-ztag', '-F', '%clientName%|%userName%', 'info')).Out -join ''
		$Client, $User = $Info -split '\|'
		if (-not $Client -or $Client -eq '*unknown*')
		{
			throw "No Perforce workspace found for $TargetPath. Set P4CLIENT, or a P4CONFIG file in the workspace."
		}
		# Each line of p4 where is a view mapping; lines starting with "-" are exclusions.
		$Mapped = (Invoke-Tool p4 @('where', (Join-Path $TargetPath '...')) -AllowFailure).Out | Where-Object { $_ -and -not $_.StartsWith('-') }
		if (-not $Mapped)
		{
			throw "$TargetPath is not in the view of Perforce workspace $Client."
		}
		return @($Client, $User)
	}
	finally
	{
		Pop-Location
	}
}

function Assert-SvnWorkingCopy([string]$TargetPath)
{
	if ((Invoke-Tool svn @('info', (Get-ExistingFolder $TargetPath)) -AllowFailure).ExitCode -ne 0)
	{
		throw "$TargetPath is not in an SVN working copy."
	}
}

function Sync-Perforce([string]$TargetPath, [string[]]$SourceFiles, [string[]]$RemovedFiles, [string]$Message, [string[]]$Workspace)
{
	Push-Location -LiteralPath $TargetPath
	try
	{
		$Client, $User = $Workspace

		# A spec without a Files field, so files already open in the default changelist stay there.
		$Spec = "Change: new`nClient: $Client`nUser: $User`nStatus: new`nDescription:`n`t$Message`n"
		$Created = (Invoke-Tool p4 @('change', '-i') -InputText $Spec).Out -join "`n"
		if ($Created -notmatch 'Change (\d+) created')
		{
			throw "Could not create a pending changelist: $Created"
		}
		$Change = $Matches[1]

		foreach ($File in $RemovedFiles)
		{
			Remove-Item -LiteralPath (Join-Path $TargetPath $File) -Force
		}

		# reconcile opens changed files for edit, new files for add and removed files for delete, comparing digests,
		# so unchanged files are not opened.
		$ListFile = [IO.Path]::GetTempFileName()
		try
		{
			[IO.File]::WriteAllLines($ListFile, [string[]]@($SourceFiles + $RemovedFiles | ForEach-Object { Join-Path $TargetPath $_ }))
			$Reconciled = Invoke-Tool p4 @('-x', $ListFile, 'reconcile', '-c', $Change) -AllowFailure
		}
		finally
		{
			Remove-Item -LiteralPath $ListFile -Force -ErrorAction SilentlyContinue
		}
		foreach ($Line in $Reconciled.Err)
		{
			if ($Line -notmatch 'no file\(s\) to reconcile')
			{
				Write-Warning $Line
			}
		}

		$Opened = @((Invoke-Tool p4 @('-ztag', '-F', '%action% %depotFile%', 'opened', '-c', $Change) -AllowFailure).Out | Where-Object { $_ })
		if ($Opened.Count -eq 0)
		{
			Invoke-Tool p4 @('change', '-d', $Change) | Out-Null
			Write-Host "Nothing to update: $TargetPath already matches $Message."
			return
		}

		Write-Host "Changelist $Change ($Message):"
		$Opened | ForEach-Object { Write-Host "  $_" }
		if ($Submit)
		{
			Invoke-Tool p4 @('submit', '-c', $Change) | Out-Null
			Write-Host "Submitted changelist $Change."
		}
		else
		{
			Write-Host "Review it, then submit with: p4 submit -c $Change"
		}
	}
	finally
	{
		Pop-Location
	}
}

function Sync-Svn([string]$TargetPath, [string[]]$RemovedFiles, [string]$Message)
{
	foreach ($File in $RemovedFiles)
	{
		$Path = Join-Path $TargetPath $File
		if ((Invoke-Tool svn @('info', $Path) -AllowFailure).ExitCode -eq 0)
		{
			Invoke-Tool svn @('delete', '--force', $Path) | Out-Null
		}
		else
		{
			Remove-Item -LiteralPath $Path -Force
		}
	}

	# Add -Target itself (and missing parents) without its contents, so the kept folders are not added with it.
	if ((Invoke-Tool svn @('info', $TargetPath) -AllowFailure).ExitCode -ne 0)
	{
		Invoke-Tool svn @('add', '--depth', 'empty', '--parents', $TargetPath) | Out-Null
	}

	[xml]$Status = (Invoke-Tool svn @('status', '--xml', $TargetPath)).Out -join "`n"
	foreach ($Entry in $Status.status.target.entry)
	{
		if ($Entry.'wc-status'.item -eq 'unversioned' -and -not (Test-KeptPath (Get-RelativePath $TargetPath $Entry.path)))
		{
			Invoke-Tool svn @('add', $Entry.path) | Out-Null
		}
	}

	$Changes = @((Invoke-Tool svn @('status', $TargetPath)).Out | Where-Object {
		$_.Length -gt 8 -and $_[0] -ne '?' -and -not (Test-KeptPath (Get-RelativePath $TargetPath $_.Substring(8)))
	})
	if ($Changes.Count -eq 0)
	{
		Write-Host "Nothing to update: $TargetPath already matches $Message."
		return
	}

	Write-Host "Scheduled in the working copy ($Message):"
	$Changes | ForEach-Object { Write-Host "  $_" }
	if ($Submit)
	{
		Invoke-Tool svn @('commit', $TargetPath, '-m', $Message) | Out-Null
		Write-Host "Committed $TargetPath."
	}
	else
	{
		Write-Host "Review it, then commit with: svn commit `"$TargetPath`" -m `"$Message`""
	}
}

$RepoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$TargetPath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Target).TrimEnd('\')
Assert-SafeTarget $TargetPath $RepoRoot

$Commit = (Invoke-Tool git @('-C', $RepoRoot, 'rev-parse', '--short', "$Revision^{commit}")).Out[0]
if ($Revision -eq 'HEAD' -and (Invoke-Tool git @('-C', $RepoRoot, 'status', '--porcelain', '--untracked-files=no')).Out.Count -gt 0)
{
	Write-Warning "$RepoRoot has uncommitted changes. They are not copied; only commit $Commit is."
}
$Message = "$PluginName $Commit"

if ($Vcs -eq 'Perforce')
{
	$Workspace = Get-PerforceWorkspace $TargetPath
}
else
{
	Assert-SvnWorkingCopy $TargetPath
}

$Staging = Join-Path ([IO.Path]::GetTempPath()) "$PluginName-$([Guid]::NewGuid().ToString('N'))"
$Archive = "$Staging.zip"
try
{
	Invoke-Tool git @('-C', $RepoRoot, 'archive', '--format=zip', "--output=$Archive", $Commit) | Out-Null
	Expand-Archive -LiteralPath $Archive -DestinationPath $Staging

	$SourceFiles = Get-Files $Staging
	$TargetFiles = Get-Files $TargetPath
	$RemovedFiles = @($TargetFiles | Where-Object { $SourceFiles -notcontains $_ })

	foreach ($File in $SourceFiles)
	{
		$Destination = Join-Path $TargetPath $File
		New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Destination) | Out-Null
		# -Force also overwrites the read-only files of a Perforce workspace.
		Copy-Item -LiteralPath (Join-Path $Staging $File) -Destination $Destination -Force
	}

	if ($Vcs -eq 'Perforce')
	{
		Sync-Perforce $TargetPath $SourceFiles $RemovedFiles $Message $Workspace
	}
	else
	{
		Sync-Svn $TargetPath $RemovedFiles $Message
	}
}
finally
{
	Remove-Item -LiteralPath $Staging -Recurse -Force -ErrorAction SilentlyContinue
	Remove-Item -LiteralPath $Archive -Force -ErrorAction SilentlyContinue
}
