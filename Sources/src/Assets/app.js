const app = document.getElementById("app");
const overlay = document.getElementById("overlay");
const chatList = document.getElementById("chatList");
const mainTitle = document.querySelector(".mainHeader h1");
const mql = window.matchMedia("(max-width: 760px)");
const DEFAULT_TITLE = "杰睿 JeriBot";
let activeConvId = null;
let activeNav = null;
const expandedGroups = new Set();

function setMainTitle(name) { mainTitle.textContent = name || DEFAULT_TITLE; }

function clearActive() {
  chatList.querySelectorAll(".convItem").forEach(el => el.classList.remove("active"));
  document.querySelectorAll(".navButton").forEach(el => el.classList.remove("active"));
}

function selectConversation(id, group, name) {
  clearActive();
  activeConvId = id;
  activeNav = null;
  ensureGroupExpanded(group);
  setMainTitle(name);
  const target = chatList.querySelector('.convItem[data-id="' + id + '"][data-group="' + group + '"]');
  if (target) target.classList.add("active");
}

function selectNav(navId, label) {
  clearActive();
  activeConvId = null;
  activeNav = navId;
  setMainTitle(label);
  const btn = document.querySelector('.navButton[data-nav="' + navId + '"]');
  if (btn) btn.classList.add("active");
}

function ensureGroupExpanded(group) {
  expandedGroups.clear();
  expandedGroups.add(group);
  chatList.querySelectorAll(".groupSection").forEach(s => {
    s.classList.toggle("collapsed", s.dataset.group !== group);
  });
}

function toggleGroup(group) {
  if (expandedGroups.has(group)) {
    expandedGroups.clear();
    chatList.querySelectorAll(".groupSection").forEach(s => s.classList.add("collapsed"));
  } else {
    ensureGroupExpanded(group);
  }
}

function isMobile() { return mql.matches; }
function openSidebar() { app.classList.remove("collapsed"); app.classList.add("open"); }
function closeSidebar() { app.classList.remove("open"); }

document.getElementById("toggleSidebar").addEventListener("click", () => {
  if (isMobile()) { app.classList.contains("open") ? closeSidebar() : openSidebar(); return; }
  app.classList.toggle("collapsed");
  document.getElementById("toggleSidebar").setAttribute("aria-label", app.classList.contains("collapsed") ? "展开侧边栏" : "折叠侧边栏");
});
overlay.addEventListener("click", closeSidebar);
document.querySelector(".mobileMenuBtn").addEventListener("click", () => { if (isMobile()) openSidebar(); });
mql.addEventListener("change", () => { if (!isMobile()) app.classList.remove("open"); });

async function api(path, options) { return (await fetch(path, options)).json(); }

function escapeHtml(s) { const d = document.createElement("div"); d.textContent = s; return d.innerHTML; }

async function loadConversations() {
  const data = await api("/api/conversation/list");
  if (data.result !== "success") return;
  renderChatList(data.data);
  if (!activeConvId && !activeNav && data.data.length > 0) {
    for (const g of data.data) {
      if (g.conversations.length > 0) {
        selectConversation(g.conversations[0].id, g.group, g.conversations[0].name || "未命名会话");
        break;
      }
    }
    if (!activeConvId) await newConversation();
  }
}

function renderChatList(groups) {
  chatList.innerHTML = "";
  let restoredActive = false;
  for (const g of groups) {
    const section = document.createElement("div");
    section.className = "groupSection" + (expandedGroups.has(g.group) ? "" : " collapsed");
    section.dataset.group = g.group;
    const isDefault = g.group === "Default";
    const displayName = isDefault ? "默认分组" : g.group;
    const folderIcon = isDefault ? "ri-folder-settings-line" : "ri-folder-line";
    let html = '<div class="groupHeader">'
      + '<i class="ri-arrow-down-s-line groupToggleIcon" aria-hidden="true"></i>'
      + '<i class="' + folderIcon + ' groupIcon" aria-hidden="true"></i>'
      + '<span class="groupName">' + escapeHtml(displayName) + '</span>'
      + (isDefault ? '' : '<button class="groupMenuBtn" type="button" title="分组菜单" aria-label="分组菜单"><i class="ri-more-2-fill" aria-hidden="true"></i></button>')
      + '</div><div class="conversationList">';
    for (const c of g.conversations) {
      const name = c.name || "未命名会话";
      if (activeConvId === c.id) restoredActive = true;
      html += '<div class="convItem' + (activeConvId === c.id ? ' active' : '') + '" data-id="' + escapeHtml(c.id) + '" data-group="' + escapeHtml(g.group) + '" data-name="' + escapeHtml(name) + '">'
        + '<span class="convInitial">' + escapeHtml(name.charAt(0)) + '</span>'
        + '<span class="convName">' + escapeHtml(name) + '</span>'
        + '<button class="convMenuBtn" type="button" title="会话菜单" aria-label="会话菜单"><i class="ri-more-2-fill" aria-hidden="true"></i></button></div>';
    }
    html += '</div>';
    section.innerHTML = html;
    chatList.appendChild(section);
  }
  if (!restoredActive) { activeConvId = null; setMainTitle(null); }
  if (activeNav) { const btn = document.querySelector('.navButton[data-nav="' + activeNav + '"]'); if (btn) btn.classList.add("active"); }
}

async function newConversation() {
  const data = await api("/api/conversation/new", { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify({ group: "Default" }) });
  if (data.result !== "success") return;
  ensureGroupExpanded("Default");
  await loadConversations();
  if (data.id) selectConversation(data.id, "Default", "未命名会话");
  if (isMobile()) closeSidebar();
}

async function deleteConversation(group, id) {
  const data = await api("/api/conversation/delete", { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify({ group, id }) });
  if (data.result === "success") {
    const wasActive = activeConvId === id;
    if (wasActive) { activeConvId = null; activeNav = null; }
    await loadConversations();
    if (wasActive && !activeConvId) {
      const list = await api("/api/conversation/list");
      if (list.result === "success") {
        for (const g of list.data) {
          if (g.conversations.length > 0) { selectConversation(g.conversations[0].id, g.group, g.conversations[0].name || "未命名会话"); break; }
        }
      }
    }
  } else if (data.message) { alert(data.message); }
}

async function renameConversation(group, id) {
  const item = chatList.querySelector('.convItem[data-id="' + id + '"][data-group="' + group + '"]');
  if (!item) return;
  startInlineEdit(item.querySelector(".convName"), (newName) => {
    if (!newName || !newName.trim()) return;
    api("/api/conversation/rename", { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify({ group, id, name: newName.trim() }) }).then(data => {
      if (data.result === "success") { if (activeConvId === id) setMainTitle(newName.trim()); loadConversations(); }
      else if (data.message) alert(data.message);
    });
  });
}

async function renameGroup(oldName) {
  const section = chatList.querySelector('.groupSection[data-group="' + oldName + '"]');
  if (!section) return;
  startInlineEdit(section.querySelector(".groupName"), (newName) => {
    if (!newName || !newName.trim() || newName.trim() === oldName) return;
    api("/api/conversation/rename-group", { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify({ old_name: oldName, new_name: newName.trim() }) }).then(data => {
      if (data.result === "success") { if (expandedGroups.has(oldName)) { expandedGroups.delete(oldName); expandedGroups.add(newName.trim()); } loadConversations(); }
      else if (data.message) alert(data.message);
    });
  });
}

function startInlineEdit(el, onCommit) {
  const oldText = el.textContent;
  const input = document.createElement("input");
  input.type = "text"; input.value = oldText; input.className = "inlineEditInput";
  el.textContent = ""; el.appendChild(input); input.focus(); input.select();
  let committed = false;
  function commit() {
    if (committed) return; committed = true;
    input.removeEventListener("blur", commit); input.removeEventListener("keydown", onKey);
    const val = input.value;
    el.textContent = val.trim() ? val : oldText;
    if (val.trim() && val.trim() !== oldText) onCommit(val); else el.textContent = oldText;
  }
  function onKey(e) {
    if (e.key === "Enter") { e.preventDefault(); commit(); }
    else if (e.key === "Escape") { committed = true; input.removeEventListener("blur", commit); input.removeEventListener("keydown", onKey); el.textContent = oldText; }
  }
  input.addEventListener("blur", commit); input.addEventListener("keydown", onKey);
}

async function deleteGroup(name) {
  const data = await api("/api/conversation/delete-group", { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify({ name }) });
  if (data.result === "success") { expandedGroups.delete(name); await loadConversations(); }
  else if (data.message) alert(data.message);
}

async function moveConversation(group, id) {
  closeMenu();
  const data = await api("/api/conversation/list");
  if (data.result !== "success") return;
  closeMenu();
  showGroupMenu(convMenuLastAnchor, data.data, (newGroup) => {
    if (newGroup === group) return;
    api("/api/conversation/move", { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify({ id, old_group: group, new_group: newGroup }) }).then(d => {
      if (d.result === "success") { ensureGroupExpanded(newGroup); loadConversations(); }
      else if (d.message) alert(d.message);
    });
  });
}

let activeMenu = null;
let convMenuLastAnchor = null;

function closeMenu() { if (activeMenu) { activeMenu.remove(); activeMenu = null; } }

function positionMenu(menu, anchor) {
  const r = anchor.getBoundingClientRect();
  let top = r.bottom + 4, left = r.left;
  if (top + menu.offsetHeight > window.innerHeight) top = r.top - menu.offsetHeight - 4;
  if (left + menu.offsetWidth > window.innerWidth) left = window.innerWidth - menu.offsetWidth - 8;
  menu.style.top = top + "px"; menu.style.left = left + "px";
}

function showContextMenu(anchor, items) {
  closeMenu();
  const menu = document.createElement("div"); menu.className = "ctxMenu";
  for (const item of items) {
    const row = document.createElement("div");
    row.className = "ctxMenuItem" + (item.danger ? " danger" : "");
    row.innerHTML = '<i class="' + escapeHtml(item.icon) + ' ctxMenuIcon" aria-hidden="true"></i><span>' + escapeHtml(item.label) + '</span>' + (item.submenu ? '<i class="ri-arrow-right-s-line ctxMenuArrow" aria-hidden="true"></i>' : '');
    row.addEventListener("click", item.submenu ? (e) => { e.stopPropagation(); item.action(); } : () => { closeMenu(); item.action(); });
    menu.appendChild(row);
  }
  document.body.appendChild(menu);
  positionMenu(menu, anchor);
  activeMenu = menu;
}

function showGroupMenu(anchor, groups, onSelect) {
  closeMenu();
  const menu = document.createElement("div"); menu.className = "ctxMenu ctxMenuScroll";
  const curGroup = convMenuLastAnchor ? convMenuLastAnchor.closest(".convItem").dataset.group : null;
  for (const g of groups) {
    const row = document.createElement("div");
    row.className = "ctxMenuItem" + (g.group === curGroup ? " ctxMenuCurrent" : "");
    row.innerHTML = '<i class="ri-folder-line ctxMenuIcon" aria-hidden="true"></i><span>' + escapeHtml(g.group === "Default" ? "默认分组" : g.group) + '</span>';
    row.addEventListener("click", () => { closeMenu(); onSelect(g.group); });
    menu.appendChild(row);
  }
  document.body.appendChild(menu);
  positionMenu(menu, anchor);
  activeMenu = menu;
}

document.addEventListener("click", (e) => { if (activeMenu && !activeMenu.contains(e.target)) closeMenu(); });

chatList.addEventListener("click", (e) => {
  const convMenu = e.target.closest(".convMenuBtn");
  if (convMenu) {
    e.stopPropagation();
    const item = convMenu.closest(".convItem");
    convMenuLastAnchor = convMenu;
    showContextMenu(convMenu, [
      { icon: "ri-edit-line", label: "重命名", action: () => renameConversation(item.dataset.group, item.dataset.id) },
      { icon: "ri-folder-transfer-line", label: "移动分组", submenu: true, action: () => moveConversation(item.dataset.group, item.dataset.id) },
      { icon: "ri-delete-bin-line", label: "删除", danger: true, action: () => deleteConversation(item.dataset.group, item.dataset.id) }
    ]);
    return;
  }
  const groupMenu = e.target.closest(".groupMenuBtn");
  if (groupMenu) {
    e.stopPropagation();
    showContextMenu(groupMenu, [
      { icon: "ri-edit-line", label: "重命名", action: () => renameGroup(groupMenu.closest(".groupSection").dataset.group) },
      { icon: "ri-delete-bin-line", label: "删除", danger: true, action: () => deleteGroup(groupMenu.closest(".groupSection").dataset.group) }
    ]);
    return;
  }
  if (e.target.closest(".groupHeader")) { toggleGroup(e.target.closest(".groupSection").dataset.group); return; }
  const convItem = e.target.closest(".convItem");
  if (convItem) selectConversation(convItem.dataset.id, convItem.dataset.group, convItem.dataset.name);
});

document.querySelector(".actionButton").addEventListener("click", newConversation);

document.querySelectorAll(".actionButton")[1].addEventListener("click", function() {
  const btn = this;
  if (btn.querySelector(".inlineEditInput")) return;
  const label = btn.querySelector(".actionLabel");
  const oldText = label.textContent;
  const input = document.createElement("input"); input.type = "text"; input.className = "inlineEditInput"; input.value = "";
  label.textContent = ""; label.appendChild(input); input.focus();
  let committed = false;
  function finish(val) {
    if (committed) return; committed = true;
    input.removeEventListener("blur", onBlur); input.removeEventListener("keydown", onKey);
    if (val && val.trim()) {
      btn.disabled = true; label.textContent = val.trim(); label.classList.add("actionLoading");
      api("/api/conversation/add-group", { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify({ name: val.trim() }) }).then(data => {
        btn.disabled = false; label.classList.remove("actionLoading");
        if (data.result === "success") { label.textContent = oldText; loadConversations(); }
        else if (data.message) {
          label.textContent = "";
          const input2 = document.createElement("input"); input2.type = "text"; input2.className = "inlineEditInput inlineEditError"; input2.value = ""; input2.placeholder = data.message;
          label.appendChild(input2); input2.focus();
          function submit2() {
            const v = input2.value.trim();
            if (!v) { label.textContent = oldText; btn.disabled = false; return; }
            label.textContent = v; label.classList.add("actionLoading"); btn.disabled = true;
            api("/api/conversation/add-group", { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify({ name: v }) }).then(d => {
              btn.disabled = false; label.classList.remove("actionLoading");
              if (d.result === "success") { label.textContent = oldText; loadConversations(); }
              else if (d.message) { input2.value = ""; input2.placeholder = d.message; }
              else label.textContent = oldText;
            });
          }
          input2.addEventListener("blur", () => { if (!btn.disabled) label.textContent = oldText; });
          input2.addEventListener("keydown", (e) => { if (e.key === "Enter") { e.preventDefault(); submit2(); } else if (e.key === "Escape") { label.textContent = oldText; btn.disabled = false; } });
        } else label.textContent = oldText;
      });
    } else label.textContent = oldText;
  }
  function onBlur() { finish(input.value); }
  function onKey(e) { if (e.key === "Enter") { e.preventDefault(); finish(input.value); } else if (e.key === "Escape") { committed = true; input.removeEventListener("blur", onBlur); input.removeEventListener("keydown", onKey); label.textContent = oldText; } }
  input.addEventListener("blur", onBlur); input.addEventListener("keydown", onKey);
});

document.querySelectorAll(".navButton").forEach(btn => {
  btn.addEventListener("click", () => selectNav(btn.dataset.nav, btn.querySelector(".navLabel").textContent));
});

loadConversations();