// Screenshot viewer. Clicking a screenshot opens it large in a <dialog> with its caption, instead
// of navigating to the bare image file. Esc, Close or a click outside the image closes it; the
// arrow keys or Previous / Next step through the screenshots. Without this script (or with
// Ctrl / middle click) the links still open the image itself.
(() => {
  const viewer = document.getElementById("viewer");
  const links = [...document.querySelectorAll(".shot > a")];
  if (!viewer || !links.length || typeof viewer.showModal !== "function") return;

  const img = document.getElementById("viewer-img");
  const caption = document.getElementById("viewer-caption");
  const count = document.getElementById("viewer-count");
  let index = 0;
  let opener = null;

  function show(i) {
    index = (i + links.length) % links.length;
    const link = links[index];
    const thumb = link.querySelector("img");
    img.src = link.getAttribute("href");
    img.alt = thumb ? thumb.alt : "";
    caption.textContent = link.parentElement.querySelector("figcaption")?.textContent.trim() ?? "";
    count.textContent = `${index + 1} of ${links.length}`;
  }

  links.forEach((link, i) => {
    link.addEventListener("click", (e) => {
      if (e.button !== 0 || e.ctrlKey || e.metaKey || e.shiftKey || e.altKey) return;
      e.preventDefault();
      opener = link;
      show(i);
      viewer.showModal();
      document.getElementById("viewer-close").focus();
    });
  });

  viewer.querySelectorAll("[data-step]").forEach((btn) =>
    btn.addEventListener("click", () => show(index + Number(btn.dataset.step))));
  document.getElementById("viewer-close").addEventListener("click", () => viewer.close());

  // A click on the backdrop lands on the dialog element itself, outside its content.
  viewer.addEventListener("click", (e) => {
    if (e.target === viewer) viewer.close();
  });

  viewer.addEventListener("keydown", (e) => {
    if (e.key === "ArrowRight") { show(index + 1); e.preventDefault(); }
    if (e.key === "ArrowLeft") { show(index - 1); e.preventDefault(); }
  });

  // Back to the screenshot that opened it, so keyboard users keep their place.
  viewer.addEventListener("close", () => opener?.focus());
})();
