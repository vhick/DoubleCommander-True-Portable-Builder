#requires -Version 7.2
<#
.SYNOPSIS
  Locate Double Commander's newest official published Windows x64 portable ZIP.
.DESCRIPTION
  Official SourceForge release directory and project Download wiki provide versions.
  The GitHub release asset digest or SourceForge's published SHA-256 verifies bytes.
  This script FAILS CLOSED if neither checksum is available for an unfamiliar version.
  No third-party update aggregators, unverified mirrors or nightly builds are used.
#>
param(
    [ValidatePattern('^$|^\d+\.\d+\.\d+$')]
    [string]$VersionOverride = ''
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$Repo = Split-Path -Parent $PSScriptRoot
$KnownHashFile = Join-Path $Repo 'scripts\verified-hashes.json'
$SourceForgeWiki = 'https://sourceforge.net/p/doublecmd/wiki/Download/'
$SourceForgeFolder = 'https://sourceforge.net/projects/doublecmd/files/Double%20Commander/'
$GitHubApi = 'https://api.github.com/repos/doublecmd/doublecmd'
$UserAgent = 'DoubleCommanderTruePortableBuilder/auto-latest (+https://github.com/vhick/DoubleCommander-True-Portable-Builder)'

function Get-OfficialHtml([string]$Uri) {
    if (-not $Uri.StartsWith('https://sourceforge.net/', [StringComparison]::Ordinal)) {
        throw "Refusing an unexpected metadata host: $Uri"
    }
    $response = Invoke-WebRequest -Uri $Uri -TimeoutSec 35 -MaximumRedirection 5 -Headers @{
        'User-Agent' = $UserAgent
        'Accept' = 'text/html,application/xhtml+xml'
    }
    $type = [string]$response.Headers['Content-Type']
    if ($type -notmatch 'text/html') { throw "Expected HTML from SourceForge but received $type" }
    return [string]$response.Content
}

# A known, independently checked hash is a safe fallback for an existing release.
# For NEW versions, the published hash must be found automatically: never skip validation.
$Known = @{}
if (Test-Path -LiteralPath $KnownHashFile) {
    $entries = Get-Content -LiteralPath $KnownHashFile -Raw | ConvertFrom-Json -AsHashtable
    foreach ($key in $entries.Keys) {
        if ($key -notmatch '^\d+\.\d+\.\d+$' -or $entries[$key] -notmatch '^[0-9a-fA-F]{64}$') {
            throw "Invalid local verified-hashes.json record: $key"
        }
        $Known[$key] = [string]$entries[$key].ToLowerInvariant()
    }
}

if ($VersionOverride) {
    $Version = $VersionOverride
} else {
    $candidates = [System.Collections.Generic.List[Version]]::new()
    $successes = 0
    # GitHub's official /releases/latest is a dependable discovery fallback when
    # SourceForge HTML is unavailable to an automated GitHub runner.
    try {
        $latest = Invoke-RestMethod -Uri "$GitHubApi/releases/latest" -TimeoutSec 30 -Headers @{
            'User-Agent' = $UserAgent
            'Accept' = 'application/vnd.github+json'
            'X-GitHub-Api-Version' = '2022-11-28'
        }
        if (-not $latest.draft -and -not $latest.prerelease -and [string]$latest.tag_name -match '^v(\d+\.\d+\.\d+)$') {
            $latestVersion = $Matches[1]
            $matchingName = "doublecmd-$latestVersion.x86_64-win64.zip"
            if (@($latest.assets | Where-Object { $_.name -ceq $matchingName }).Count -eq 1) {
                $candidates.Add([Version]$latestVersion)
                $successes++
            }
        }
    } catch { Write-Warning "Official GitHub latest-release API cannot be read: $($_.Exception.Message)" }
    try {
        $html = Get-OfficialHtml $SourceForgeWiki
        $successes++
        foreach ($m in [regex]::Matches($html, 'doublecmd-(?<v>\d+\.\d+\.\d+)\.x86_64-win64\.zip', 'IgnoreCase')) {
            $candidates.Add([Version]$m.Groups['v'].Value)
        }
    } catch { Write-Warning "Official download wiki cannot be read: $($_.Exception.Message)" }
    try {
        $html = Get-OfficialHtml $SourceForgeFolder
        $successes++
        # Only official version directories, not 'latest', nightly/alpha, or development tags.
        foreach ($m in [regex]::Matches($html, '(?i)\bv(?<v>\d+\.\d+\.\d+)(?![0-9a-z._-])')) {
            $candidates.Add([Version]$m.Groups['v'].Value)
        }
    } catch { Write-Warning "Official SourceForge release directory cannot be read: $($_.Exception.Message)" }
    if ($candidates.Count -eq 0) {
        throw "Unable to resolve a published Windows x64 release from official SourceForge metadata (pages checked: $successes). No fallback to an outdated hardcoded version."
    }
    $Version = [string](($candidates | Sort-Object -Descending | Select-Object -First 1))
}
if ($Version -notmatch '^\d+\.\d+\.\d+$') { throw "Unsafe version string: $Version" }
$FileName = "doublecmd-$Version.x86_64-win64.zip"
$ReleaseFileUrl = "https://sourceforge.net/projects/doublecmd/files/Double%20Commander/v$Version/$FileName/download"
$DirectSourceForge = "https://downloads.sourceforge.net/project/doublecmd/Double%20Commander/v$Version/$FileName"

# First preference: GitHub's first-party, per-asset sha256 digest if this release is uploaded.
$GithubChecksum = $null
$GithubAssetUrl = $null
try {
    $release = Invoke-RestMethod -Uri "$GitHubApi/releases/tags/v$Version" -TimeoutSec 30 -Headers @{
        'User-Agent' = $UserAgent
        'Accept' = 'application/vnd.github+json'
        'X-GitHub-Api-Version' = '2022-11-28'
    }
    if ($release.draft -or $release.prerelease) {
        Write-Warning "Upstream GitHub v$Version is marked as prerelease/draft; SourceForge will be used."
    } else {
        $asset = @($release.assets | Where-Object { $_.name -ceq $FileName })
        if ($asset.Count -eq 1 -and [string]$asset[0].digest -match '^sha256:([0-9a-fA-F]{64})$') {
            $GithubChecksum = $Matches[1].ToLowerInvariant()
            $expectedUrl = "https://github.com/doublecmd/doublecmd/releases/download/v$Version/$FileName"
            if ($asset[0].browser_download_url -ceq $expectedUrl) {
                $GithubAssetUrl = $expectedUrl
            }
        }
    }
} catch {
    Write-Host "GitHub metadata unavailable for v${Version}; checking SourceForge: $($_.Exception.Message)"
}

# SourceForge download landing pages display a published SHA-256 (not a hash calculated
# from our own download). If they redirect to the binary, do not interpret it as metadata.
$SourceForgeChecksum = $null
try {
    $page = Get-OfficialHtml $ReleaseFileUrl
    $match = [regex]::Match($page, '(?is)SHA[\s\-]*256\s*:?.{0,800}?(?<sha>[0-9a-f]{64})(?![0-9a-f])')
    if ($match.Success) { $SourceForgeChecksum = $match.Groups['sha'].Value.ToLowerInvariant() }
} catch {
    Write-Host "SourceForge SHA-256 landing page unavailable: $($_.Exception.Message)"
}

if ($GithubChecksum -and $SourceForgeChecksum -and $GithubChecksum -ne $SourceForgeChecksum) {
    throw "Upstream checksum disagreement for $FileName (GitHub=$GithubChecksum, SourceForge=$SourceForgeChecksum). Refusing to build."
}
$ExpectedSha256 = if ($GithubChecksum) { $GithubChecksum } elseif ($SourceForgeChecksum) { $SourceForgeChecksum } elseif ($Known.ContainsKey($Version)) { $Known[$Version] } else { $null }
if (-not $ExpectedSha256) {
    throw "No official SHA-256 could be retrieved for new release v$Version. Failing safely: do not publish unverified application binaries."
}
if ($Known.ContainsKey($Version) -and $Known[$Version] -ne $ExpectedSha256) {
    throw "Official SHA-256 differs from independently pinned digest for v$Version. Stop for manual investigation."
}
$DownloadUrl = if ($GithubAssetUrl) { $GithubAssetUrl } else { $DirectSourceForge }
Write-Host "Official Double Commander version: $Version"
Write-Host "Selected archive: $FileName"
Write-Host "Expected SHA-256: $ExpectedSha256"
Write-Host "Download source: $DownloadUrl"
return [pscustomobject]@{
    version = $Version
    sha256 = $ExpectedSha256
    download_url = $DownloadUrl
    filename = $FileName
}
