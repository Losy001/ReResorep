# Going open source + website: checklist

Notes on whether to publish ReResorep as open source with a website, and what to do first.
Short answer: yes. At v2.5.2 it has a solid README, credits, tests and packaging, so it is
close to ready.

## Before making the repo public

- [ ] **Add a `LICENSE` file.** There isn't one yet. Without a license, the code is legally
      "all rights reserved", so nobody can fork or contribute even if they can see it.
  - **MIT**: simplest and most permissive. It matches Dear ImGui, which is already bundled
    under MIT (`third_party/imgui/LICENSE.txt`).
  - **GPL-3.0**: if you want forks to stay open source too.
- [ ] **Keep the Resorep wording in `CREDITS.md`.** It already says no Resorep code is
      included and that the hash, `resorep1` pack format and file naming were worked out from
      Resorep's proxy DLL. That's the right way to present a compatible rewrite.
- [ ] **Keep the anti-cheat warning prominent.** Put the same BattlEye warning on the website,
      because people arriving from a site will skim. Avoid wording that sounds like it's for
      online play, and don't use Ubisoft or Siege logos and art. A text mention of
      "Rainbow Six Siege" with the "not affiliated" line is fine.
- [ ] **Look through the git history before you flip it public.** All past commits become
      visible, so check for anything you wouldn't want seen (personal paths, test files,
      notes): `git log -p`, or search with `git log -S "<text>"`.
- [ ] **Mind the DX12 limitation.** Current Siege (Y10S1+) is DX12-only, so many visitors won't
      be able to use it yet. Put that near the top of the site so people don't download it and
      get confused (see `docs/DX12.md`).

## The website (keep it small)

- [ ] Host on **GitHub Pages**. It's free and builds straight from the repo (for example a
      `docs/` folder or a `gh-pages` branch). It doesn't need its own domain.
- [ ] One page is enough:
  - what it is (one or two sentences)
  - a screenshot or GIF of the manager
  - a download button pointing to **GitHub Releases**
  - the DX11-only and anti-cheat warnings
  - a link to the repo
- [ ] Publish `ReResorep.exe` and `proxy\d3d11.dll` as **Release assets** rather than
      committing binaries. The `packaging/proxy.sha256` setup fits well with this, because
      users can check what they downloaded.

## Don't wait for "done"

Open source projects are rarely finished. Releasing at 2.5.2 and continuing with 2.6 etc. is
normal, and early users will find bugs you won't.

## Possible next steps

1. Pick a license and add `LICENSE` at the repo root (and mention it in the README).
2. Make a simple GitHub Pages landing page.
3. Make a GitHub Release for v2.5.2 with the exe and proxy attached.
4. Make the repository public (GitHub → Settings → General → Danger Zone → Change visibility).
