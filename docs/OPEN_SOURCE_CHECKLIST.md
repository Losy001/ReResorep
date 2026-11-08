# Going open source + website: checklist

Notes on publishing ReResorep as open source with a website, and what to do first.
Status as of 2026-11-08: done. The repo is public, 2.5.3 is released with the GPL, and the
site is live at https://sosigdoggo.github.io/ReResorep/.

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
- [x] **Host on GitHub Pages.** `.github/workflows/pages.yml` publishes `docs/site` whenever it
      changes on `main` (or from the Actions tab). Pages is set to "GitHub Actions".
- [x] **Release assets, not committed binaries.** Releases attach the zip (exe, proxy, README,
      credits, notices, checksums) and its SHA-256; `packaging/proxy.sha256` pins the proxy.

## Don't wait for "done"

Open source projects are rarely finished. Releasing now and continuing with 2.6 etc. is normal,
and early users will find bugs you won't.

## What's left, in order

1. ~~Remove the email from old commits.~~ Done.
2. ~~Add the screenshots to the site.~~ Done: Mods, Dumps and Settings, with no personal paths.
3. ~~Make the repository public.~~ Done on 2026-11-08.
4. ~~Turn on GitHub Pages and run the "Website" workflow.~~ Done: the site is live at
   https://sosigdoggo.github.io/ReResorep/ and is the repo's homepage, with a description and
   topics set.
5. ~~Show the GPL in the app's **About** tab.~~ Done in 2.5.3: a Licence section with the
   copyright line, a short summary, the full text and a link to the source.
