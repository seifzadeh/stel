# Release and Pages guide

Repository: <https://github.com/seifzadeh/stel>  
Website: <https://seifzadeh.github.io/stel/>

The prepared tree contains source, build/packaging scripts, workflows, licenses,
documentation and website assets. It contains no `.deps`, local builds, binaries,
account configuration, PBX credentials or private keys. No commit, push, release,
workflow run or repository-setting change was made during local preparation.

## First publication

1. In repository **Settings → Actions → General**, allow GitHub Actions and the
   actions used in the workflows. The release job requests `contents: write`;
   repository/organization policy must permit it. No personal access token or
   manually added repository secret is required.
2. In **Settings → Pages → Build and deployment → Source**, select **GitHub
   Actions**. This is the one-time setup for the prepared Pages workflow.
3. Review and commit the prepared source tree, then push `main`:

   ```sh
   cd /home/dev/codes/stel/repo
   git status --short
   git add .
   git commit -m "Prepare Stel 0.6.0 and automated releases"
   git push origin main
   ```

The **Build, test and release** workflow starts automatically. It builds/tests
Linux and Windows and checks the standalone applications. Only when both jobs
succeed does it create the `v0.6.0` tag and GitHub Release for that commit. A draft
is used while uploading; it is published after all assets upload successfully.
The **Website** workflow publishes the static site to GitHub Pages separately.
The first SDK build on Windows takes longer; later runs reuse pinned dependency
caches. Workflow logs are the authority on the first Windows build's outcome.

## Next versions

```sh
python3 scripts/release.py bump 0.6.1
# Add a "## 0.6.1" section to CHANGELOG.md and describe the changes.
git add .
git commit -m "Release Stel 0.6.1"
git push origin main
```

No manual tag is needed. A manual `vX.Y.Z` tag or **Run workflow** on `main` also
works; a tag must exactly match the CMake version. Pushing code without increasing
an already-published version still runs the build/tests but does not replace its
published release or assets. A failed build publishes nothing. A release upload
failure can leave a draft; retry the same commit to finish it, or review/remove
the draft before trying another commit with that version. Do not overwrite a
published version: bump the version and publish a new release.

Pull requests build/test but never publish releases. Forks cannot use the release
job's token to publish to the upstream repository. Local `release.py` only prepares
files; it has no GitHub operations. `scripts/ci/publish-release.py` refuses to run
outside GitHub Actions.

## Release contents

- `Stel-X.Y.Z-linux-x86_64.run` and `.tar.gz`
- `Stel-X.Y.Z-windows-x64.exe` and `.zip`
- `Stel-source-X.Y.Z.tar.gz`
- `Stel-X.Y.Z-dependencies-linux-sources.tar.gz`
- `Stel-X.Y.Z-dependencies-windows-sources.tar.gz`
- `SHA256SUMS`

SHA-256 covers all release assets. Dependency sources are collected from the
exact SDK/submodule revisions or distro source versions used by each build;
licenses and notices travel with the binaries. These packages are not signed.
Windows SmartScreen may therefore show an unknown-publisher prompt.

Current Linux builds use Debian 13; check the bundle manifest's minimum glibc.
Windows tests run on Windows 2022, not Windows 7/8.1. Mark legacy compatibility
verified only after testing the published binaries on those systems.

## Website preview

```sh
python3 scripts/build-site.py
python3 -m http.server 8877 --directory _site
```

Open <http://localhost:8877>. The static site needs no npm packages or hosted
backend. It supports English/Persian, responsive screenshots and a screenshot
viewer. Download buttons resolve the latest published release; before the first
release, or if the GitHub API is unavailable, they open the releases page.
App screenshots come from local test accounts, not from private PBX captures.

GitHub references: [publishing with GitHub CLI](https://docs.github.com/en/actions/how-tos/write-workflows/choose-what-workflows-do/use-github-cli),
[Pages deployment source](https://docs.github.com/en/pages/getting-started-with-github-pages/configuring-a-publishing-source-for-your-github-pages-site).
