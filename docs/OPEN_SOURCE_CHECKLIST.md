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
- [x] **Your email in commit history.** Done on 2026-11-08: every commit and the `v2.5.3` tag
      use the GitHub noreply address, and the repo was recreated so the old commits are gone from
      GitHub too. Only 2.5.3 is released; older versions are kept locally. To keep it that way,
      set the global git email to the noreply address and tick GitHub → Settings → Emails →
      "Keep my email addresses private" and "Block command line pushes that expose my email".
- [x] **Mind the DX12 limitation.** It's the first item in the site's "Before you start" panel.

## The website (keep it small)

- [x] **One page** in `docs/site`: what it is, the DX11 and anti-cheat notes, setup steps,
      features, a download button pointing to GitHub Releases, and a link to the repo.
- [x] **Screenshots of the manager.** The Mods, Dumps and Settings tabs are in
      `docs/site/img`, cropped to the app's content (no title bar or status bar), with dummy
      folder paths in the Settings shot.
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

1. ~~Remove the email from old commits.~~ Done.
2. ~~Add the screenshots to the site.~~ Done: Mods, Dumps and Settings, with no personal paths.
3. Make the repository public: GitHub → Settings → General → Danger Zone → Change visibility.
   The Download button on the site only works for visitors once this is done.
4. Turn on GitHub Pages (Source: GitHub Actions) and run the "Website" workflow.
5. Optional: show the GPL in the app's **About** tab next to the other credits.
