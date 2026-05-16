const app = document.getElementById("app");
const overlay = document.getElementById("overlay");
const mql = window.matchMedia("(max-width: 760px)");

function isMobile() { return mql.matches; }

function openSidebar() {
  app.classList.remove("collapsed");
  app.classList.add("open");
}

function closeSidebar() {
  app.classList.remove("open");
}

document.getElementById("toggleSidebar").addEventListener("click", () => {
  if (isMobile()) {
    app.classList.contains("open") ? closeSidebar() : openSidebar();
    return;
  }
  app.classList.toggle("collapsed");
  const c = app.classList.contains("collapsed");
  document.getElementById("toggleSidebar").setAttribute("aria-label", c ? "展开侧边栏" : "折叠侧边栏");
});

overlay.addEventListener("click", closeSidebar);

document.querySelector(".mobileMenuBtn").addEventListener("click", () => {
  if (isMobile()) openSidebar();
});

mql.addEventListener("change", () => {
  if (!isMobile()) app.classList.remove("open");
});