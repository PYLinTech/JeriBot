const app = document.getElementById("app");
const toggleSidebar = document.getElementById("toggleSidebar");
const sidebarOverlay = document.getElementById("sidebarOverlay");
const mobileSidebarButtons = document.querySelectorAll(".mobileSidebarButton");

function isMobileLayout() {
  return window.matchMedia("(max-width: 760px)").matches;
}

function openMobileSidebar() {
  if (!isMobileLayout()) return;
  app.classList.add("mobileSidebarOpen");
}

function closeMobileSidebar() {
  app.classList.remove("mobileSidebarOpen");
}

toggleSidebar.addEventListener("click", () => {
  if (isMobileLayout()) {
    openMobileSidebar();
  } else {
    app.classList.toggle("sidebarCollapsed");
    const collapsed = app.classList.contains("sidebarCollapsed");
    toggleSidebar.setAttribute("aria-label", collapsed ? "展开侧边栏" : "折叠侧边栏");
    toggleSidebar.setAttribute("title", collapsed ? "展开侧边栏" : "折叠侧边栏");
  }
});

sidebarOverlay.addEventListener("click", closeMobileSidebar);

for (const button of mobileSidebarButtons) {
  button.addEventListener("click", openMobileSidebar);
}