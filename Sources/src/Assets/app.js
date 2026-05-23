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
  document.getElementById("storeView").classList.remove("active");
  document.getElementById("mainView").classList.add("active");
  const target = chatList.querySelector('.convItem[data-id="' + id + '"][data-group="' + group + '"]');
  if (target) target.classList.add("active");
}

function selectNav(navId, label) {
  clearActive();
  activeConvId = null;
  activeNav = navId;
  const btn = document.querySelector('.navButton[data-nav="' + navId + '"]');
  if (btn) btn.classList.add("active");

  const mainView = document.getElementById("mainView");
  const storeView = document.getElementById("storeView");
  if (navId === "store") {
    mainView.classList.remove("active");
    storeView.classList.add("active");
    loadStoreSource().finally(() => loadStore(storeState.category));
  } else {
    storeView.classList.remove("active");
    mainView.classList.add("active");
    setMainTitle(label);
  }
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
document.querySelectorAll(".mobileMenuBtn").forEach(btn => btn.addEventListener("click", () => { if (isMobile()) openSidebar(); }));
mql.addEventListener("change", () => { if (!isMobile()) app.classList.remove("open"); });

async function api(path, options) {
  const response = await fetch(path, options);
  const text = await response.text();
  let data = {};
  if (text) {
    try { data = JSON.parse(text); } catch { data = { result: "error", message: text }; }
  }
  if (!response.ok) {
    throw new Error(data.message || `${response.status} ${response.statusText}`);
  }
  return data;
}

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
  btn.addEventListener("click", () => { selectNav(btn.dataset.nav, btn.querySelector(".navLabel").textContent); if (isMobile()) closeSidebar(); });
});

const STORE_TYPES = {
  personality: "人格",
  skill: "技能",
  memory: "记忆",
  tool: "工具",
  local: "本地"
};

const STORE_SOURCE_LABELS = {
  china: "国内源",
  global: "国际源",
  custom: "自定义源"
};

const storeState = {
  category: "personality",
  source: "",
  sourceUrl: "",
  sourceRawUrl: "",
  sources: [],
  packages: [],
  installed: { byId: {}, byKey: {}, list: [] },
  search: "",
  loading: false,
  localPack: null,
  requestSeq: 0
};

function categoryLabel(cat) { return STORE_TYPES[cat] || cat || "-"; }
function sourceLabel(source) { return STORE_SOURCE_LABELS[source] || source || "-"; }

function storeEls() {
  return {
    sourcePicker: document.getElementById("storeSourcePicker"),
    sourceButton: document.getElementById("storeSourceButton"),
    sourceButtonText: document.getElementById("storeSourceButtonText"),
    sourceMenu: document.getElementById("storeSourceMenu"),
    editCustomSourceBtn: document.getElementById("storeEditCustomSourceBtn"),
    searchInput: document.getElementById("storeSearchInput"),
    message: document.getElementById("storeMessage"),
    remotePane: document.getElementById("storeRemotePane"),
    localPane: document.getElementById("storeLocalPane"),
    stats: document.getElementById("storeStats"),
    list: document.getElementById("storeItemList"),
    dropZone: document.getElementById("storeZipDropZone"),
    fileInput: document.getElementById("storeZipInput"),
    localCard: document.getElementById("storeLocalPackCard"),
    installLocalBtn: document.getElementById("installLocalPackBtn")
  };
}

function setStoreMessage(text = "", type = "") {
  const { message } = storeEls();
  message.textContent = text;
  message.className = "storeMessage";
  if (type) message.classList.add(type);
}

function setStoreLoading(loading) {
  storeState.loading = loading;
  const { sourceButton, editCustomSourceBtn, installLocalBtn } = storeEls();
  sourceButton.disabled = loading;
  editCustomSourceBtn.disabled = loading;
  installLocalBtn.disabled = loading || !storeState.localPack?.temp_id;
  document.querySelectorAll(".storeActionButton").forEach(btn => { btn.disabled = loading; });
}

function normalizeInstalled(data) {
  const raw = data?.installed && typeof data.installed === "object" ? data.installed : {};
  const result = { byId: {}, byKey: {}, list: [] };
  for (const key of Object.keys(raw)) {
    const entry = raw[key] && typeof raw[key] === "object" ? raw[key] : {};
    const dot = key.indexOf(".");
    const uploader = entry.uploader || (dot > 0 ? key.slice(0, dot) : "");
    const id = entry.id || (dot > 0 ? key.slice(dot + 1) : key);
    const item = { ...entry, id, uploader, key };
    result.list.push(item);
    if (id) result.byId[id] = item;
    if (uploader && id) result.byKey[`${uploader}.${id}`] = item;
  }
  return result;
}

function packageInstallKey(pkg) {
  return pkg?.uploader && pkg?.id ? `${pkg.uploader}.${pkg.id}` : (pkg?.id || "");
}

function getInstalledForPackage(pkg) {
  return storeState.installed.byKey[packageInstallKey(pkg)] || storeState.installed.byId[pkg?.id || ""];
}

function getInstalledIconUrl(type, id) {
  return `/api/store/icon?type=${encodeURIComponent(type)}&id=${encodeURIComponent(id)}`;
}

function getLocalIconUrl(tempId) {
  return `/api/store/icon?temp_id=${encodeURIComponent(tempId)}`;
}

function getRemoteIconUrl(type, id) {
  return `/api/store/icon?remote=1&type=${encodeURIComponent(type)}&id=${encodeURIComponent(id)}`;
}

function resolveStoreIcon(icon) {
  const value = String(icon || "").trim();
  if (!value) return "";
  if (/^data:image\//i.test(value)) return value;
  if (/^https?:\/\//i.test(value)) return value;
  if (/^[A-Za-z0-9+/=]+$/.test(value) && value.length > 64) return `data:image/x-icon;base64,${value}`;
  try {
    const base = storeState.sourceUrl ? new URL(storeState.sourceUrl).href : window.location.href;
    return new URL(value, base).href;
  } catch {
    return "";
  }
}

function isInlineStoreIcon(icon) {
  const value = String(icon || "").trim();
  return /^data:image\//i.test(value) || (/^[A-Za-z0-9+/=]+$/.test(value) && value.length > 64);
}

function packageIconUrl(pkg, installed) {
  if (installed) return getInstalledIconUrl(storeState.category, pkg.id);
  if (pkg?.id && pkg?.icon && !isInlineStoreIcon(pkg.icon)) {
    return getRemoteIconUrl(storeState.category, pkg.id);
  }
  return resolveStoreIcon(pkg?.icon);
}

function bindStoreIcon(img, iconUrl, fallbackIconClass) {
  if (!img) return;
  const icon = img.parentElement.querySelector("i");
  img.classList.add("hidden");
  img.removeAttribute("src");
  if (icon) icon.style.display = "";
  if (!iconUrl) return;

  img.onload = () => {
    img.classList.remove("hidden");
    if (icon) icon.style.display = "none";
  };
  img.onerror = () => {
    img.classList.add("hidden");
    if (icon) icon.style.display = "";
  };
  if (fallbackIconClass && icon) icon.className = fallbackIconClass;
  img.src = iconUrl;
}

function mergeStorePackages(remotePackages, installedData) {
  const merged = [];
  const seen = new Set();
  for (const pkg of remotePackages) {
    const key = packageInstallKey(pkg);
    if (key) seen.add(key);
    if (pkg?.id) seen.add(pkg.id);
    merged.push(pkg);
  }
  for (const item of installedData.list) {
    const fullKey = item.uploader && item.id ? `${item.uploader}.${item.id}` : item.id;
    if (!item.id || seen.has(fullKey) || seen.has(item.id)) continue;
    seen.add(fullKey || item.id);
    merged.push({
      id: item.id,
      uploader: item.uploader,
      name: item.name || item.id,
      description: item.description || "已安装到本地",
      version: item.version,
      icon: item.icon,
      installedOnly: true
    });
  }
  return merged;
}

function getFilteredPackages() {
  const q = storeState.search.trim().toLowerCase();
  if (!q) return storeState.packages;
  return storeState.packages.filter(pkg => [
    pkg.name,
    pkg.id,
    pkg.uploader,
    pkg.author,
    pkg.description,
    pkg.version
  ].some(v => String(v || "").toLowerCase().includes(q)));
}

async function loadStoreSource() {
  const { sourceButtonText, sourceMenu, editCustomSourceBtn } = storeEls();
  try {
    const data = await api("/api/store/source");
    storeState.source = data.current || "";
    storeState.sourceUrl = data.url || "";
    storeState.sourceRawUrl = data.raw_url || data.url || "";
    storeState.sources = Array.isArray(data.sources) ? data.sources : [];

    sourceButtonText.textContent = sourceLabel(storeState.source);
    sourceMenu.innerHTML = "";
    for (const src of storeState.sources) {
      const option = document.createElement("button");
      option.type = "button";
      option.className = "storeSourceOption";
      option.dataset.source = src.name || "";
      option.setAttribute("role", "option");
      option.setAttribute("aria-selected", src.name === storeState.source ? "true" : "false");
      option.innerHTML = '<span></span><small></small><i class="ri-check-line" aria-hidden="true"></i>';
      option.querySelector("span").textContent = sourceLabel(src.name);
      option.querySelector("small").textContent = src.url || "尚未设置";
      option.addEventListener("click", () => switchStoreSource(src.name || ""));
      sourceMenu.appendChild(option);
    }
    editCustomSourceBtn.classList.toggle("hidden", storeState.source !== "custom");
  } catch (e) {
    setStoreMessage(`源设置读取失败：${e.message || e}`, "error");
  }
}

async function loadStore(category = storeState.category) {
  const seq = ++storeState.requestSeq;
  storeState.category = category;
  document.querySelectorAll(".storeTab").forEach(btn => {
    btn.classList.toggle("active", btn.dataset.storeCat === category);
  });

  const isLocal = category === "local";
  const { remotePane, localPane, searchInput } = storeEls();
  remotePane.classList.toggle("hidden", isLocal);
  localPane.classList.toggle("hidden", !isLocal);
  searchInput.disabled = isLocal;

  if (isLocal) {
    renderLocalPackPreview();
    setStoreMessage("");
    return;
  }

  setStoreLoading(true);
  setStoreMessage(`正在加载${categoryLabel(category)}...`, "info");
  try {
    const [remoteResult, installedResult] = await Promise.allSettled([
      api(`/api/store/list?type=${encodeURIComponent(category)}`),
      api(`/api/store/installed?type=${encodeURIComponent(category)}`)
    ]);

    const remoteData = remoteResult.status === "fulfilled" ? remoteResult.value : null;
    const installedData = installedResult.status === "fulfilled" ? installedResult.value : null;
    if (seq !== storeState.requestSeq) return;
    storeState.installed = normalizeInstalled(installedData);
    storeState.packages = mergeStorePackages(Array.isArray(remoteData?.packages) ? remoteData.packages : [], storeState.installed);
    renderStoreItems();
    if (remoteResult.status === "rejected" && installedResult.status === "fulfilled") {
      setStoreMessage(`当前源连接失败，已显示本地内容：${remoteResult.reason?.message || remoteResult.reason}`, "error");
    } else if (installedResult.status === "rejected" && remoteResult.status === "fulfilled") {
      setStoreMessage(`列表已加载，本地内容读取失败：${installedResult.reason?.message || installedResult.reason}`, "error");
    } else if (remoteResult.status === "rejected" && installedResult.status === "rejected") {
      setStoreMessage(`加载失败：${remoteResult.reason?.message || remoteResult.reason}`, "error");
    } else {
      setStoreMessage("");
    }
  } catch (e) {
    if (seq !== storeState.requestSeq) return;
    storeState.packages = [];
    storeState.installed = { byId: {}, byKey: {}, list: [] };
    renderStoreItems();
    setStoreMessage(`加载失败：${e.message || e}`, "error");
  } finally {
    if (seq === storeState.requestSeq) setStoreLoading(false);
  }
}

function renderStoreItems() {
  const { list, stats } = storeEls();
  const filtered = getFilteredPackages();
  const installedCount = storeState.packages.filter(pkg => !!getInstalledForPackage(pkg)).length;
  stats.textContent = `${categoryLabel(storeState.category)} · 共 ${storeState.packages.length} 项 · 已安装 ${installedCount} 项`;
  list.innerHTML = "";

  if (filtered.length === 0) {
    const empty = document.createElement("div");
    empty.className = "storeEmptyState";
    empty.innerHTML = '<i class="ri-inbox-line" aria-hidden="true"></i><strong></strong>';
    empty.querySelector("strong").textContent = storeState.search ? "没有找到相关内容" : "这里还没有内容";
    list.appendChild(empty);
    return;
  }

  for (const pkg of filtered) {
    const installed = getInstalledForPackage(pkg);
    const card = document.createElement("article");
    card.className = "storePackage";
    card.innerHTML = `
      <div class="storePackageIcon"><img class="storePackageImage hidden" alt=""><i aria-hidden="true"></i></div>
      <div class="storePackageBody">
        <div class="storePackageHead">
          <strong></strong>
          <span class="storeBadge"></span>
        </div>
        <p class="storePackageDesc"></p>
        <div class="storePackageMeta"></div>
      </div>
      <button class="storeActionButton" type="button"></button>`;

    const fallbackIconClass = {
      personality: "ri-user-smile-line",
      skill: "ri-magic-line",
      memory: "ri-brain-line",
      tool: "ri-tools-line"
    }[storeState.category] || "ri-archive-line";
    card.querySelector(".storePackageIcon i").className = fallbackIconClass;
    bindStoreIcon(card.querySelector(".storePackageImage"), packageIconUrl(pkg, installed), fallbackIconClass);
    card.querySelector(".storePackageHead strong").textContent = pkg.name || pkg.id || "未命名内容";
    card.querySelector(".storeBadge").textContent = `v${pkg.version || "-"}`;
    card.querySelector(".storePackageDesc").textContent = pkg.description || "暂无说明";

    const meta = [];
    if (pkg.uploader || pkg.author) meta.push(pkg.uploader || pkg.author);
    if (pkg.id) meta.push(pkg.id);
    if (pkg.installedOnly) meta.push("本地");
    if (installed) meta.push(`已安装 ${installed.version || pkg.version || ""}`.trim());
    card.querySelector(".storePackageMeta").textContent = meta.join(" · ");

    const action = card.querySelector(".storeActionButton");
    action.classList.toggle("danger", !!installed);
    action.innerHTML = installed
      ? '<i class="ri-delete-bin-line" aria-hidden="true"></i><span>卸载</span>'
      : '<i class="ri-download-cloud-2-line" aria-hidden="true"></i><span>安装</span>';
    action.addEventListener("click", () => {
      if (installed) uninstallItem(storeState.category, pkg.id, pkg.name || pkg.id);
      else installRemoteItem(storeState.category, pkg.id, pkg.name || pkg.id);
    });

    list.appendChild(card);
  }
}

async function installRemoteItem(type, id, name) {
  if (!type || !id) return;
  setStoreLoading(true);
  setStoreMessage(`正在安装 ${name}...`, "info");
  try {
    const data = await api("/api/store/remote", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ type, id })
    });
    if (data.result !== "success") throw new Error(data.message || "安装失败");
    await loadStore(type);
    setStoreMessage(`已安装 ${name}`, "success");
  } catch (e) {
    setStoreMessage(`安装失败：${e.message || e}`, "error");
  } finally {
    setStoreLoading(false);
  }
}

async function uninstallItem(type, id, name) {
  if (!type || !id) return;
  setStoreLoading(true);
  setStoreMessage(`正在卸载 ${name}...`, "info");
  try {
    const data = await api("/api/store/uninstall", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ type, id })
    });
    if (data.result !== "success") throw new Error(data.message || "卸载失败");
    await loadStore(type);
    setStoreMessage(`已卸载 ${name}`, "success");
  } catch (e) {
    setStoreMessage(`卸载失败：${e.message || e}`, "error");
  } finally {
    setStoreLoading(false);
  }
}

async function handleLocalZipUpload(file) {
  if (!file || !file.name.toLowerCase().endsWith(".zip")) {
    setStoreMessage("请选择 zip 文件", "error");
    return;
  }

  storeState.localPack = null;
  renderLocalPackPreview();
  setStoreLoading(true);
  setStoreMessage(`正在读取 ${file.name}...`, "info");
  try {
    const response = await fetch("/api/store/local", {
      method: "POST",
      body: await file.arrayBuffer()
    });
    const text = await response.text();
    let data = {};
    try { data = text ? JSON.parse(text) : {}; } catch { data = { message: text }; }
    if (!response.ok || data.result !== "success") throw new Error(data.message || "读取失败");
    storeState.localPack = { ...data, fileName: file.name };
    renderLocalPackPreview();
    setStoreMessage(`已识别 ${data.name || data.id || file.name}`, "success");
  } catch (e) {
    setStoreMessage(`读取失败：${e.message || e}`, "error");
  } finally {
    setStoreLoading(false);
  }
}

function renderLocalPackPreview() {
  const pack = storeState.localPack;
  const { localCard, installLocalBtn } = storeEls();
  localCard.classList.toggle("hidden", !pack);
  installLocalBtn.disabled = storeState.loading || !pack?.temp_id;
  const previewIcon = document.getElementById("storeLocalPackIcon");
  if (!pack) {
    previewIcon.classList.add("hidden");
    previewIcon.removeAttribute("src");
    return;
  }

  document.getElementById("storeLocalPackName").textContent = pack.name || pack.id || "未命名内容";
  document.getElementById("storeLocalPackFile").textContent = pack.fileName || "本地 zip";
  document.getElementById("storeLocalPackVersion").textContent = `v${pack.version || "-"}`;
  document.getElementById("storeLocalPackType").textContent = categoryLabel(pack.type);
  document.getElementById("storeLocalPackId").textContent = pack.id || "-";
  document.getElementById("storeLocalPackUploader").textContent = pack.uploader || "-";
  document.getElementById("storeLocalPackDesc").textContent = pack.description || "暂无说明";
  previewIcon.classList.add("hidden");
  previewIcon.onerror = () => { previewIcon.classList.add("hidden"); };
  previewIcon.onload = () => { previewIcon.classList.remove("hidden"); };
  const iconUrl = pack.temp_id ? getLocalIconUrl(pack.temp_id) : resolveStoreIcon(pack.icon);
  if (iconUrl) previewIcon.src = iconUrl;
  else previewIcon.removeAttribute("src");
}

async function installLocalPack() {
  const pack = storeState.localPack;
  if (!pack?.type || !pack?.id || !pack?.temp_id) return;

  setStoreLoading(true);
  setStoreMessage(`正在安装 ${pack.name || pack.id}...`, "info");
  try {
    const data = await api("/api/store/install", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ type: pack.type, id: pack.id, temp_id: pack.temp_id })
    });
    if (data.result !== "success") throw new Error(data.message || "安装失败");
    const installedType = pack.type;
    storeState.localPack = null;
    renderLocalPackPreview();
    setStoreMessage(`已安装 ${pack.name || pack.id}`, "success");
    if (storeState.category === installedType) await loadStore(installedType);
  } catch (e) {
    setStoreMessage(`安装失败：${e.message || e}`, "error");
  } finally {
    setStoreLoading(false);
  }
}

async function switchStoreSource(source) {
  if (!source || source === storeState.source) return;

  hideStoreSourceMenu();
  hideCustomSourceEditor();
  setStoreLoading(true);
  setStoreMessage(`正在切换到${sourceLabel(source)}...`, "info");
  try {
    const data = await api("/api/store/switch-source", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ source })
    });
    if (data.result !== "success") throw new Error(data.message || "切换失败");
    await loadStoreSource();
    await loadStore(storeState.category);
    setStoreMessage(`已切换到${sourceLabel(source)}`, "success");
  } catch (e) {
    setStoreMessage(`切换失败：${e.message || e}`, "error");
  } finally {
    setStoreLoading(false);
  }
}

function toggleStoreSourceMenu() {
  const { sourceButton, sourceMenu } = storeEls();
  const open = sourceMenu.classList.contains("hidden");
  sourceMenu.classList.toggle("hidden", !open);
  sourceButton.setAttribute("aria-expanded", open ? "true" : "false");
}

function hideStoreSourceMenu() {
  const { sourceButton, sourceMenu } = storeEls();
  sourceMenu.classList.add("hidden");
  sourceButton.setAttribute("aria-expanded", "false");
}

function hideCustomSourceEditor() {
  const editor = document.getElementById("storeCustomSourceEditor");
  if (editor) editor.remove();
}

function showCustomSourceEditor(anchor) {
  hideCustomSourceEditor();
  const editor = document.createElement("div");
  editor.id = "storeCustomSourceEditor";
  editor.className = "storeCustomSourceEditor";
  editor.innerHTML = `
    <label>
      <span>自定义源地址</span>
      <input id="storeCustomSourceInput" type="text" autocomplete="off">
    </label>
    <button id="storeSaveCustomSourceBtn" type="button"><i class="ri-check-line" aria-hidden="true"></i><span>保存</span></button>`;
  document.body.appendChild(editor);

  const input = editor.querySelector("#storeCustomSourceInput");
  input.value = storeState.sourceRawUrl || "";
  input.focus();
  input.select();

  const rect = anchor.getBoundingClientRect();
  const width = Math.min(420, Math.max(280, window.innerWidth - 24));
  editor.style.width = `${width}px`;
  let left = rect.right - width;
  let top = rect.bottom + 8;
  left = Math.max(8, Math.min(left, window.innerWidth - width - 8));
  if (top + 132 > window.innerHeight) top = Math.max(8, rect.top - 132);
  editor.style.left = `${left}px`;
  editor.style.top = `${top}px`;

  editor.querySelector("#storeSaveCustomSourceBtn").addEventListener("click", saveCustomSourceUrl);
  input.addEventListener("keydown", e => {
    if (e.key === "Enter") {
      e.preventDefault();
      saveCustomSourceUrl();
    } else if (e.key === "Escape") {
      hideCustomSourceEditor();
    }
  });
}

async function saveCustomSourceUrl() {
  const input = document.getElementById("storeCustomSourceInput");
  const url = input ? input.value.trim() : "";
  if (!url) {
    setStoreMessage("请填写自定义源地址", "error");
    return;
  }
  setStoreLoading(true);
  setStoreMessage("正在保存自定义源...", "info");
  try {
    const data = await api("/api/store/custom-source", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ url })
    });
    if (data.result !== "success") throw new Error(data.message || "保存失败");
    hideCustomSourceEditor();
    await loadStoreSource();
    if (storeState.source !== "custom") {
      await switchStoreSource("custom");
    } else {
      await loadStore(storeState.category);
      setStoreMessage("自定义源已保存", "success");
    }
  } catch (e) {
    setStoreMessage(`保存失败：${e.message || e}`, "error");
  } finally {
    setStoreLoading(false);
  }
}

function setupStore() {
  const { dropZone, localPane, fileInput, searchInput, sourceButton, editCustomSourceBtn, installLocalBtn } = storeEls();

  document.querySelectorAll(".storeTab").forEach(btn => {
    btn.addEventListener("click", () => loadStore(btn.dataset.storeCat));
  });

  searchInput.addEventListener("input", () => {
    storeState.search = searchInput.value;
    renderStoreItems();
  });

  sourceButton.addEventListener("click", toggleStoreSourceMenu);
  editCustomSourceBtn.addEventListener("click", e => {
    e.stopPropagation();
    showCustomSourceEditor(editCustomSourceBtn);
  });
  document.addEventListener("click", e => {
    const { sourcePicker } = storeEls();
    if (!sourcePicker.contains(e.target)) hideStoreSourceMenu();
    const editor = document.getElementById("storeCustomSourceEditor");
    if (editor && !editor.contains(e.target) && !editCustomSourceBtn.contains(e.target)) hideCustomSourceEditor();
  });

  installLocalBtn.addEventListener("click", installLocalPack);
  dropZone.addEventListener("click", () => fileInput.click());
  fileInput.addEventListener("change", e => {
    const file = e.target.files && e.target.files[0];
    if (file) handleLocalZipUpload(file);
    fileInput.value = "";
  });

  [dropZone, localPane].forEach(el => {
    el.addEventListener("dragover", e => {
      e.preventDefault();
      dropZone.classList.add("dragging");
    });
    el.addEventListener("dragleave", e => {
      if (!el.contains(e.relatedTarget)) dropZone.classList.remove("dragging");
    });
    el.addEventListener("drop", e => {
      e.preventDefault();
      dropZone.classList.remove("dragging");
      const file = e.dataTransfer.files && e.dataTransfer.files[0];
      if (file) handleLocalZipUpload(file);
    });
  });
}

setupStore();

loadConversations();
