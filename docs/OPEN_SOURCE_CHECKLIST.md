# Going open source + website: checklist

Notes on publishing ReResorep as open source with a website, and what to do first.
Status as of 2.5.3 (2026-11-08): everything that can be done in the repo is done. What's left
is a screenshot, a few GitHub settings and flipping the repo public.

## Before making the repo public

- [x] **Add a `LICENSE` file.** ReResorep is under the **GNU GPL v3.0** (`LICENSE`, the
      official text). The README and `CREDITS.md` say so, the site footer links it, and release
      zips include it as `LICENSE.txt` from the next release on. Dear ImGui keeps its MIT license
      (`third_party/imgui/LICENSE.txt`), which is compatible with the GPL.
- [x] **Keep the Resorep wording in `CREDITS.md`.** Unchanged: no Resorep code is included, and
      the hash, `resorep1` pack format and file naming were worked out from Resorep's proxy DLL.
- [x] **Keep the anti-cheat warning prominent.** The site has a "Before you start" panel right
      under the hero with the BattlEye warning. No Ubisoft or Siege logos or art are used, and
      the footer has the "not affiliated" line.
- [x] **Look through the git history.** Checked all commits on 2026-11-08: no personal paths, no
      keys or tokens, no deleted files that would resurface, no committed binaries.
- [ ] **Your email in commit history.** Your Gmail address is the author email on the commits
      up to 2.5.3, and it becomes visible when the repo is public. New commits in this repo now
      use your GitHub noreply address (`git config user.email`). Still to decide: how to deal
      with the old commits. Options:
  - Leave them (most people do).
  - Rewrite the history with `git filter-repo --mailmap`. This changes every commit hash, needs
    a force-push, and the `v2.x` tags and releases would have to be recreated. Do it before the
    repo is public, or not at all.
  - Also tick GitHub → Settings → Emails → "Keep my email addresses private" and "Block command
    line pushes that expose my email", so it can't happen again from another machine.
- [x] **Mind the DX12 limitation.** It's the first item in the site's "Before you start" panel.

## The website (keep it small)

- [x] **One page** in `docs/site`: what it is, the DX11 and anti-cheat notes, setup steps,
      features, a download button pointing to GitHub Releases, and a link to the repo.
- [ ] **A screenshot of the manager.** The page has a slot for it, commented out in
      `docs/site/index.html`. Save it as `docs/site/screenshot.png` (about 1600px wide, ideally
      showing only your own mod textures, no game art), set its width and height in the `<img>`,
      and remove the comment markers.
- [ ] **Host on GitHub Pages.** `.github/workflows/pages.yml` publishes `docs/site` whenever it
      changes on `main` (or from the Actions tab). After the repo is public, turn it on in
      GitHub → Settings → Pages → Source: **GitHub Actions**, then run the "Website" workflow
      once. Pages on a private repo needs a paid plan, so until then the workflow fails if it runs.
- [x] **Release assets, not committed binaries.** Releases attach the zip (exe, proxy, README,
      credits, notices, checksums) and its SHA-256; `packaging/proxy.sha256` pins the proxy.

## Don't wait for "done"

Open source projects are rarely finished. Releasing now and continuing with 2.6 etc. is normal,
and early users will find bugs you won't.

## What's left, in order

1. Decide about the email in old commits (above). Rewriting has to happen before step 3.
2. Add the screenshot to the site.
3. Make the repository public: GitHub → Settings → General → Danger Zone → Change visibility.
   The Download button on the site only works for visitors once this is done.
4. Turn on GitHub Pages (Source: GitHub Actions) and run the "Website" workflow.
5. Optional: show the GPL in the app's **About** tab next to the other credits.
