[CmdletBinding()]
param(
    [string]$Repo = "Hazelight/UnrealEngine-Angelscript",
    [string]$Branch = "angelscript-master",
    [ValidateRange(1, 100)]
    [int]$Count = 20,
    [ValidateRange(1, 100)]
    [int]$Page = 1,
    [string]$BaseSha,
    [string]$HeadSha,
    [string]$StatePath,
    [switch]$UpdateState,
    [switch]$Json
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Get-ProjectRoot {
    $scriptDir = Split-Path -Parent $PSCommandPath
    return (Resolve-Path (Join-Path $scriptDir "..\..\..\..")).Path
}

function Get-DefaultStatePath {
    $scriptDir = Split-Path -Parent $PSCommandPath
    return (Join-Path $scriptDir "..\references\audit-state.json")
}

function Get-AuditState {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    if (-not (Test-Path -LiteralPath $Path)) {
        return $null
    }

    $content = Get-Content -LiteralPath $Path -Raw
    if ([string]::IsNullOrWhiteSpace($content)) {
        return $null
    }

    return ($content | ConvertFrom-Json)
}

function Save-AuditState {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,
        [Parameter(Mandatory = $true)]
        [pscustomobject]$State
    )

    $directory = Split-Path -Parent $Path
    if (-not (Test-Path -LiteralPath $directory)) {
        New-Item -ItemType Directory -Path $directory | Out-Null
    }

    $State | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $Path -Encoding UTF8
}

function Get-IniValue {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,
        [Parameter(Mandatory = $true)]
        [string]$Section,
        [Parameter(Mandatory = $true)]
        [string]$Key
    )

    if (-not (Test-Path -LiteralPath $Path)) {
        return $null
    }

    $currentSection = $null
    foreach ($line in Get-Content -LiteralPath $Path) {
        $trimmed = $line.Trim()
        if ($trimmed.Length -eq 0 -or $trimmed.StartsWith(";") -or $trimmed.StartsWith("#")) {
            continue
        }

        if ($trimmed -match '^\[(.+)\]$') {
            $currentSection = $Matches[1]
            continue
        }

        if ($currentSection -eq $Section -and $trimmed -match '^\s*([^=]+?)\s*=\s*(.*?)\s*$') {
            if ($Matches[1].Trim() -eq $Key) {
                return $Matches[2].Trim()
            }
        }
    }

    return $null
}

function Invoke-GhJson {
    param(
        [Parameter(Mandatory = $true)]
        [string[]]$Arguments
    )

    $output = & gh @Arguments 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw "gh command failed: gh $($Arguments -join ' ')`n$output"
    }

    if ([string]::IsNullOrWhiteSpace(($output -join "`n"))) {
        return $null
    }

    return (($output -join "`n") | ConvertFrom-Json)
}

function Invoke-GitText {
    param(
        [Parameter(Mandatory = $true)]
        [string]$RepoPath,
        [Parameter(Mandatory = $true)]
        [string[]]$Arguments
    )

    $output = & git -C $RepoPath @Arguments 2>$null
    if ($LASTEXITCODE -ne 0) {
        return $null
    }

    return (($output -join "`n").Trim())
}

function ConvertTo-LocalArea {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    $rules = @(
        @{
            Pattern = '^Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Binds/'
            Area = 'Plugins/Angelscript/Source/AngelscriptRuntime/Binds'
            Classification = 'Review'
        },
        @{
            Pattern = '^Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/ClassGenerator/'
            Area = 'Plugins/Angelscript/Source/AngelscriptRuntime/ClassGenerator'
            Classification = 'Needs OpenSpec'
        },
        @{
            Pattern = '^Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/StaticJIT/'
            Area = 'Plugins/Angelscript/Source/AngelscriptRuntime/StaticJIT'
            Classification = 'Needs OpenSpec'
        },
        @{
            Pattern = '^Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/Testing/'
            Area = 'Plugins/Angelscript/Source/AngelscriptRuntime/Testing or Plugins/Angelscript/Source/AngelscriptTest'
            Classification = 'Review'
        },
        @{
            Pattern = '^Engine/Plugins/Angelscript/Source/AngelscriptCode/Public/'
            Area = 'Plugins/Angelscript/Source/AngelscriptRuntime/Public or runtime public headers'
            Classification = 'Review'
        },
        @{
            Pattern = '^Engine/Plugins/Angelscript/Source/AngelscriptEditor/'
            Area = 'Plugins/Angelscript/Source/AngelscriptEditor'
            Classification = 'Review'
        },
        @{
            Pattern = '^Engine/Plugins/AngelscriptGAS/'
            Area = 'Out of project scope by default'
            Classification = 'Out of scope'
        },
        @{
            Pattern = '^Script-Examples/'
            Area = 'Script/Examples or functional-test migration references'
            Classification = 'Review'
        }
    )

    foreach ($rule in $rules) {
        if ($Path -match $rule.Pattern) {
            return [pscustomobject]@{
                Path = $Path
                LocalArea = $rule.Area
                InitialClassification = $rule.Classification
            }
        }
    }

    return [pscustomobject]@{
        Path = $Path
        LocalArea = 'No direct mapping'
        InitialClassification = 'Reference only'
    }
}

function Test-UnrealEngineFollowSignal {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Message
    )

    return ($Message -match "Merge remote-tracking branch 'epicgames/" `
        -or $Message -match '(?i)\b(unreal engine|ue\s*5|release)\b' `
        -or $Message -match '\b5\.\d+(\.\d+)?\b')
}

function ConvertFrom-GhCommit {
    param(
        [Parameter(Mandatory = $true)]
        [pscustomobject]$Commit
    )

    $title = (($Commit.commit.message -split "`n", 2)[0])
    $message = $Commit.commit.message
    return [pscustomobject]@{
        sha = $Commit.sha.Substring(0, [Math]::Min(12, $Commit.sha.Length))
        fullSha = $Commit.sha
        date = $Commit.commit.author.date
        title = $title
        message = $message
        author = $Commit.commit.author.name
        authorEmail = $Commit.commit.author.email
        committer = $Commit.commit.committer.name
        unrealEngineFollowSignal = (Test-UnrealEngineFollowSignal -Message $message)
    }
}

function Group-CommitAuthors {
    param(
        [Parameter(Mandatory = $true)]
        [object[]]$Commits
    )

    return @(
        $Commits |
            Group-Object -Property author |
            Sort-Object -Property Count -Descending |
            ForEach-Object {
                [pscustomobject]@{
                    name = $_.Name
                    commitCount = $_.Count
                }
            }
    )
}

function Select-UnrealEngineFollowSignals {
    param(
        [Parameter(Mandatory = $true)]
        [object[]]$Commits
    )

    return @(
        $Commits | Where-Object { $_.unrealEngineFollowSignal } | ForEach-Object {
            [pscustomobject]@{
                sha = $_.sha
                fullSha = $_.fullSha
                date = $_.date
                author = $_.author
                title = $_.title
            }
        }
    )
}

function Get-LocalCloneStatus {
    param(
        [Parameter(Mandatory = $true)]
        [string]$ProjectRoot
    )

    $configPath = Join-Path $ProjectRoot "AgentConfig.ini"
    $clonePath = Get-IniValue -Path $configPath -Section "References" -Key "HazelightAngelscriptEngineRoot"

    $status = [ordered]@{
        configuredPath = $clonePath
        exists = $false
        isGitRepo = $false
        branch = $null
        head = $null
        remote = $null
    }

    if ([string]::IsNullOrWhiteSpace($clonePath) -or -not (Test-Path -LiteralPath $clonePath)) {
        return [pscustomobject]$status
    }

    $status.exists = $true
    $gitDir = Invoke-GitText -RepoPath $clonePath -Arguments @("rev-parse", "--git-dir")
    if ([string]::IsNullOrWhiteSpace($gitDir)) {
        return [pscustomobject]$status
    }

    $status.isGitRepo = $true
    $status.branch = Invoke-GitText -RepoPath $clonePath -Arguments @("branch", "--show-current")
    $status.head = Invoke-GitText -RepoPath $clonePath -Arguments @("rev-parse", "--short=12", "HEAD")
    $status.remote = Invoke-GitText -RepoPath $clonePath -Arguments @("remote", "get-url", "origin")
    return [pscustomobject]$status
}

function Format-TextReport {
    param(
        [Parameter(Mandatory = $true)]
        [pscustomobject]$Audit
    )

    Write-Output "Hazelight update audit"
    Write-Output "Repo: $($Audit.repo)"
    Write-Output "Branch: $($Audit.branch)"
    Write-Output "Audited at: $($Audit.auditedAt)"
    Write-Output "State path: $($Audit.state.path)"
    Write-Output "Previous reviewed HEAD: $($Audit.state.previousLastReviewedHeadSha)"
    if ($Audit.compare) {
        Write-Output "Compare: $($Audit.compare.base)...$($Audit.compare.head)"
        Write-Output "Ahead by: $($Audit.compare.aheadBy)"
    }

    Write-Output ""
    Write-Output "Local clone"
    Write-Output "Path: $($Audit.localClone.configuredPath)"
    Write-Output "Exists: $($Audit.localClone.exists)"
    Write-Output "Git repo: $($Audit.localClone.isGitRepo)"
    if ($Audit.localClone.isGitRepo) {
        Write-Output "Branch: $($Audit.localClone.branch)"
        Write-Output "HEAD: $($Audit.localClone.head)"
        Write-Output "Remote: $($Audit.localClone.remote)"
    }

    Write-Output ""
    Write-Output "Recent commits"
    foreach ($commit in $Audit.commits) {
        $signal = if ($commit.unrealEngineFollowSignal) { " [UE-follow]" } else { "" }
        Write-Output "$($commit.sha) $($commit.date) $($commit.author) $signal $($commit.title)"
    }

    if (@($Audit.rangeCommits).Count -gt 0) {
        Write-Output ""
        Write-Output "Range commits"
        foreach ($commit in $Audit.rangeCommits) {
            $signal = if ($commit.unrealEngineFollowSignal) { " [UE-follow]" } else { "" }
            Write-Output "$($commit.sha) $($commit.date) $($commit.author) $signal $($commit.title)"
        }
    }

    if (@($Audit.authors).Count -gt 0) {
        Write-Output ""
        Write-Output "Authors"
        foreach ($author in $Audit.authors) {
            Write-Output "$($author.name): $($author.commitCount)"
        }
    }

    if (@($Audit.unrealEngineFollowSignals).Count -gt 0) {
        Write-Output ""
        Write-Output "UE-following signals"
        foreach ($commit in $Audit.unrealEngineFollowSignals) {
            Write-Output "$($commit.sha) $($commit.title)"
        }
    }

    if (@($Audit.files).Count -gt 0) {
        Write-Output ""
        Write-Output "Changed files"
        foreach ($file in $Audit.files) {
            Write-Output "$($file.initialClassification) | $($file.localArea) | $($file.path)"
        }
    }
}

$projectRoot = Get-ProjectRoot
$resolvedStatePath = if ([string]::IsNullOrWhiteSpace($StatePath)) { Get-DefaultStatePath } else { $StatePath }
$resolvedStatePath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($resolvedStatePath)
$auditState = Get-AuditState -Path $resolvedStatePath

$ghCommand = Get-Command gh -ErrorAction SilentlyContinue
if (-not $ghCommand) {
    throw "GitHub CLI 'gh' was not found on PATH."
}

$repoInfo = Invoke-GhJson -Arguments @("repo", "view", $Repo, "--json", "nameWithOwner,visibility,defaultBranchRef,url,updatedAt")
$localClone = Get-LocalCloneStatus -ProjectRoot $projectRoot

if ([string]::IsNullOrWhiteSpace($BaseSha) -and $auditState -and -not [string]::IsNullOrWhiteSpace($auditState.lastReviewedHeadSha)) {
    $BaseSha = $auditState.lastReviewedHeadSha
}

$commitEndpoint = "repos/$Repo/commits?sha=$Branch&per_page=$Count&page=$Page"
$commitsRaw = Invoke-GhJson -Arguments @("api", $commitEndpoint)
$commits = @(
    foreach ($commit in $commitsRaw) {
        ConvertFrom-GhCommit -Commit $commit
    }
)

$compareInfo = $null
$compareRaw = $null
$rangeCommits = @()
$changedFiles = @()
if (-not [string]::IsNullOrWhiteSpace($BaseSha)) {
    $resolvedHead = if ([string]::IsNullOrWhiteSpace($HeadSha)) { $Branch } else { $HeadSha }
    $compareEndpoint = "repos/$Repo/compare/$BaseSha...$resolvedHead"
    $compareRaw = Invoke-GhJson -Arguments @("api", $compareEndpoint)
    $compareInfo = [pscustomobject]@{
        base = $BaseSha
        head = $resolvedHead
        status = $compareRaw.status
        aheadBy = $compareRaw.ahead_by
        behindBy = $compareRaw.behind_by
        totalCommits = $compareRaw.total_commits
    }

    $rangeCommits = @(
        foreach ($commit in $compareRaw.commits) {
            ConvertFrom-GhCommit -Commit $commit
        }
    )

    $changedFiles = @(
        foreach ($file in $compareRaw.files) {
            $mapped = ConvertTo-LocalArea -Path $file.filename
            [pscustomobject]@{
                path = $mapped.Path
                status = $file.status
                additions = $file.additions
                deletions = $file.deletions
                changes = $file.changes
                localArea = $mapped.LocalArea
                initialClassification = $mapped.InitialClassification
            }
        }
    )
}

$classificationCommits = if ($rangeCommits.Count -gt 0) { $rangeCommits } else { $commits }
$authors = Group-CommitAuthors -Commits $classificationCommits
$unrealEngineFollowSignals = Select-UnrealEngineFollowSignals -Commits $classificationCommits

$stateSummary = [pscustomobject]@{
    path = $resolvedStatePath
    exists = [bool]$auditState
    previousLastReviewedHeadSha = if ($auditState) { $auditState.lastReviewedHeadSha } else { $null }
    previousLastReviewedAt = if ($auditState) { $auditState.lastReviewedAt } else { $null }
    updateRequested = [bool]$UpdateState
}

$audit = [pscustomobject]@{
    repo = $repoInfo.nameWithOwner
    repoUrl = $repoInfo.url
    visibility = $repoInfo.visibility
    branch = $Branch
    page = $Page
    count = $Count
    upstreamDefaultBranch = $repoInfo.defaultBranchRef.name
    upstreamUpdatedAt = $repoInfo.updatedAt
    auditedAt = (Get-Date).ToUniversalTime().ToString("yyyy-MM-ddTHH:mm:ssZ")
    state = $stateSummary
    localClone = $localClone
    compare = $compareInfo
    commits = $commits
    rangeCommits = $rangeCommits
    authors = $authors
    unrealEngineFollowSignals = $unrealEngineFollowSignals
    files = $changedFiles
}

if ($UpdateState) {
    $newHeadSha = if ($compareRaw -and $compareRaw.merge_base_commit) {
        if ($compareRaw.commits.Count -gt 0) {
            $compareRaw.commits[$compareRaw.commits.Count - 1].sha
        } else {
            $HeadSha
        }
    } elseif ($commits.Count -gt 0) {
        $commits[0].fullSha
    } else {
        $HeadSha
    }

    $stateToSave = [pscustomobject]@{
        repo = $audit.repo
        branch = $audit.branch
        lastReviewedHeadSha = $newHeadSha
        lastReviewedAt = $audit.auditedAt
        lastAuditBaseSha = if ($compareInfo) { $compareInfo.base } else { $stateSummary.previousLastReviewedHeadSha }
        lastAuditHeadSha = $newHeadSha
        lastAuthors = $authors
        lastUnrealEngineFollowSignals = $unrealEngineFollowSignals
        notes = if ($auditState -and $auditState.notes) { $auditState.notes } else { "" }
    }

    Save-AuditState -Path $resolvedStatePath -State $stateToSave
    $audit.state | Add-Member -NotePropertyName updatedLastReviewedHeadSha -NotePropertyValue $newHeadSha
}

if ($Json) {
    $audit | ConvertTo-Json -Depth 8
} else {
    Format-TextReport -Audit $audit
}
