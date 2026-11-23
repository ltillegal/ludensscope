/* LudensScope presentation site — shared behaviour for both languages. */

/* Sticky header state */
const header = document.getElementById("header");
const onScroll = () => {
  if (!header) return;
  header.classList.toggle("scrolled", window.scrollY > 10);
};
window.addEventListener("scroll", onScroll, { passive: true });
onScroll();

/* Mobile navigation */
const nav = document.getElementById("nav");
const menuToggle = document.getElementById("menu-toggle");
if (menuToggle && nav) {
  menuToggle.addEventListener("click", () => {
    const open = nav.classList.toggle("open");
    menuToggle.setAttribute("aria-expanded", open ? "true" : "false");
  });
  nav.querySelectorAll("a").forEach((a) => {
    a.addEventListener("click", () => {
      nav.classList.remove("open");
      menuToggle.setAttribute("aria-expanded", "false");
    });
  });
}

/* Scroll reveal */
const revealEls = document.querySelectorAll(".reveal");
if ("IntersectionObserver" in window) {
  const observer = new IntersectionObserver((entries) => {
    entries.forEach((entry) => {
      if (entry.isIntersecting) {
        entry.target.classList.add("visible");
        observer.unobserve(entry.target);
      }
    });
  }, { threshold: 0.12, rootMargin: "0px 0px -40px 0px" });

  revealEls.forEach((el) => observer.observe(el));
} else {
  revealEls.forEach((el) => el.classList.add("visible"));
}

/* Theme screenshots: click a palette to swap the preview */
const themePicker = document.querySelector("[data-theme-picker]");
const themeShot = document.getElementById("theme-shot");
if (themePicker && themeShot) {
  const buttons = Array.from(themePicker.querySelectorAll("button[data-shot]"));
  const loaded = new Set();
  let swapToken = 0;

  const preload = (src) => {
    if (!src || loaded.has(src)) return;
    loaded.add(src);
    new Image().src = src;
  };

  const select = (btn) => {
    const src = btn.dataset.shot;
    if (!src || themeShot.getAttribute("src") === src) return;

    buttons.forEach((b) => b.setAttribute("aria-pressed", String(b === btn)));

    const token = ++swapToken;
    themeShot.classList.add("is-swapping");
    window.setTimeout(() => {
      if (token !== swapToken) return;
      themeShot.src = src;
      const prefix = themeShot.dataset.altPrefix || "";
      themeShot.alt = (prefix + " " + (btn.dataset.name || "")).trim();
      requestAnimationFrame(() => {
        requestAnimationFrame(() => themeShot.classList.remove("is-swapping"));
      });
    }, 120);
  };

  buttons.forEach((btn) => {
    btn.addEventListener("mouseenter", () => preload(btn.dataset.shot), { once: true });
    btn.addEventListener("focus", () => preload(btn.dataset.shot), { once: true });
    btn.addEventListener("click", () => select(btn));
  });

  // Warm the remaining shots once the section is close to the viewport.
  const themeSection = themePicker.closest("section") || themePicker;
  if ("IntersectionObserver" in window) {
    const warm = new IntersectionObserver((entries) => {
      entries.forEach((entry) => {
        if (!entry.isIntersecting) return;
        buttons.forEach((btn) => preload(btn.dataset.shot));
        warm.disconnect();
      });
    }, { rootMargin: "300px" });
    warm.observe(themeSection);
  }
}

/* Latest release version (graceful fallback to the static string) */
const releaseApi = "https://api.github.com/repos/ltillegal/ludensscope/releases/latest";
fetch(releaseApi, { headers: { Accept: "application/vnd.github+json" } })
  .then((res) => (res.ok ? res.json() : null))
  .then((data) => {
    if (!data || !data.tag_name) return;
    const tag = String(data.tag_name);
    document.querySelectorAll(".js-version").forEach((el) => { el.textContent = tag; });
  })
  .catch(() => {});
