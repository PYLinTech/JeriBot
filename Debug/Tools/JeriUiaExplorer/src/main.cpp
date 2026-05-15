#include <windows.h>
#include <objbase.h>
#include <oleauto.h>
#include <commctrl.h>
#include <windowsx.h>
#include <UIAutomationClient.h>
#include <commdlg.h>

#include <string>
#include <sstream>
#include <vector>
#include <algorithm>
#include <cstdlib>
#include <cwctype>
#include <iterator>
#include <climits>
#include <unordered_map>

#pragma comment(lib, "Comctl32.lib")
#pragma comment(lib, "Ole32.lib")
#pragma comment(lib, "OleAut32.lib")
#pragma comment(lib, "Uiautomationcore.lib")

#define IDC_TREE 1001
#define IDC_DETAIL 1002
#define IDC_REFRESH 1003
#define IDC_STATUS 1004
#define IDC_AUTOMARK 1005
#define IDC_EXPORT 1006
#define IDC_INTERVAL_LABEL 1007
#define IDC_INTERVAL_EDIT 1008
#define IDC_SEARCH_EDIT 1009
#define IDC_SEARCH_BUTTON 1010
#define IDC_SEARCH_PREV 1011
#define IDC_SEARCH_NEXT 1012
#define IDC_REVERSE_LOOKUP 1013
#define IDM_ANNOTATE 2001
#define IDM_CLEAR_ANNOTATION 2002
#define IDM_EXPAND_ALL 2003
#define IDM_EXPORT_THIS 2004
#define IDM_UIA_INVOKE 2101
#define IDM_UIA_TOGGLE 2102
#define IDM_UIA_SELECT 2103
#define IDM_UIA_EXPAND 2104
#define IDM_UIA_COLLAPSE 2105
#define IDM_UIA_SCROLL_INTO_VIEW 2106
#define IDM_UIA_SET_FOCUS 2107
#define IDM_UIA_WINDOW_CLOSE 2108
#define IDM_UIA_WINDOW_MINIMIZE 2109
#define IDM_UIA_WINDOW_MAXIMIZE 2110
#define IDM_UIA_WINDOW_RESTORE 2111
#define IDM_MOUSE_MOVE_CENTER 2201
#define IDM_MOUSE_LEFT_CLICK 2202
#define IDM_MOUSE_LEFT_DOUBLE_CLICK 2203
#define IDM_MOUSE_RIGHT_CLICK 2204
#define IDM_MOUSE_DRAG_TO_CURSOR 2205
#define IDT_AUTO_REFRESH 3001
#define IDT_REVERSE_LOOKUP 3002
#define WM_APP_REVERSE_LOOKUP_STOP (WM_APP + 1)

static const UINT DEFAULT_AUTO_REFRESH_INTERVAL_MS = 1000;
static UINT g_autoRefreshIntervalMs = DEFAULT_AUTO_REFRESH_INTERVAL_MS;

struct NodeData;

static HINSTANCE g_hInst = nullptr;
static HWND g_hMain = nullptr;
static HWND g_hTree = nullptr;
static HWND g_hDetail = nullptr;
static HWND g_hRefresh = nullptr;
static HWND g_hExport = nullptr;
static HWND g_hStatus = nullptr;
static HWND g_hAutoMark = nullptr;
static HWND g_hIntervalLabel = nullptr;
static HWND g_hIntervalEdit = nullptr;
static HWND g_hSearchEdit = nullptr;
static HWND g_hSearchButton = nullptr;
static HWND g_hSearchPrev = nullptr;
static HWND g_hSearchNext = nullptr;
static HWND g_hReverseLookup = nullptr;
static IUIAutomation* g_uia = nullptr;
static IUIAutomationTreeWalker* g_controlWalker = nullptr;
static HWND g_hOverlay[4] = { nullptr, nullptr, nullptr, nullptr };
static HBRUSH g_hOverlayBrush = nullptr;
static bool g_hasAnnotation = false;
static HTREEITEM g_annotatedItem = nullptr;
static NodeData* g_annotatedNode = nullptr;
static bool g_autoAnnotate = false;
static bool g_dragSplitter = false;
static bool g_ctrlMultiClick = false;
static bool g_refreshPaused = false;
static bool g_isRefreshing = false;
static int g_splitterX = 360;
static std::vector<HTREEITEM> g_selectedItems;
static std::wstring g_lastSearchKeyword;
static std::vector<HTREEITEM> g_searchMatches;
static int g_searchIndex = -1;
static std::vector<HTREEITEM> g_searchScopeItems;
static std::wstring g_statusText;
static std::vector<HTREEITEM> g_changedItems;
static int g_treeRedrawLock = 0;
static bool g_reverseLookupActive = false;
static HTREEITEM g_reverseLookupWindowItem = nullptr;
static HTREEITEM g_reverseLookupLastItem = nullptr;

struct ReverseLookupCandidate {
    HTREEITEM item = nullptr;
    RECT rect{};
    int depth = 0;
    long long area = 0;
};

static std::vector<ReverseLookupCandidate> g_reverseLookupCandidates;
static POINT g_reverseLookupLastPoint{ LONG_MIN, LONG_MIN };
static std::wstring g_reverseLookupLastStatus;

static const int MAX_TREE_OPERATION_NODES = 10000;
static const int MAX_EXPAND_ALL_NODES = MAX_TREE_OPERATION_NODES;
static const int MAX_SEARCH_NODES = MAX_TREE_OPERATION_NODES;
static const int MAX_REVERSE_LOOKUP_DEPTH = 80;
static const int MAX_REVERSE_LOOKUP_NODES = MAX_TREE_OPERATION_NODES;
static const UINT REVERSE_LOOKUP_TIMER_MS = 160;

static IUIAutomationTreeWalker* GetControlViewWalker() {
    if (!g_uia) return nullptr;
    if (!g_controlWalker) {
        IUIAutomationTreeWalker* walker = nullptr;
        if (FAILED(g_uia->get_ControlViewWalker(&walker)) || !walker) return nullptr;
        g_controlWalker = walker;
    }
    return g_controlWalker;
}

static void SetWindowEnabledIfChanged(HWND hwnd, BOOL enabled) {
    if (!hwnd) return;
    if (IsWindowEnabled(hwnd) != enabled) EnableWindow(hwnd, enabled);
}

static void SetWindowTextIfChanged(HWND hwnd, const wchar_t* text) {
    if (!hwnd) return;
    const wchar_t* value = text ? text : L"";
    int len = GetWindowTextLengthW(hwnd);
    std::wstring current(static_cast<size_t>(len + 1), L'\0');
    GetWindowTextW(hwnd, &current[0], len + 1);
    current.resize(static_cast<size_t>(len));
    if (current != value) SetWindowTextW(hwnd, value);
}

struct ScopedTreeRedraw {
    bool active = false;
    explicit ScopedTreeRedraw(bool enable = true) {
        active = enable && g_hTree;
        if (!active) return;
        if (g_treeRedrawLock++ == 0) {
            SendMessageW(g_hTree, WM_SETREDRAW, FALSE, 0);
        }
    }
    ~ScopedTreeRedraw() {
        if (!active) return;
        if (g_treeRedrawLock > 0) --g_treeRedrawLock;
        if (g_treeRedrawLock == 0) {
            SendMessageW(g_hTree, WM_SETREDRAW, TRUE, 0);
            RedrawWindow(g_hTree, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
        }
    }
};

static void RedrawTransparentChild(HWND child) {
    if (!child) return;
    HWND parent = GetParent(child);
    if (parent) {
        RECT rc{};
        GetWindowRect(child, &rc);
        MapWindowPoints(nullptr, parent, reinterpret_cast<POINT*>(&rc), 2);
        RedrawWindow(parent, &rc, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW | RDW_NOCHILDREN);
    }
    RedrawWindow(child, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW);
}

static void SetStatusText(const wchar_t* text) {
    if (!g_hStatus) return;
    std::wstring value = text ? text : L"";
    if (value == g_statusText) return;
    g_statusText = value;
    RedrawTransparentChild(g_hStatus);
    SetWindowTextW(g_hStatus, value.c_str());
    RedrawTransparentChild(g_hStatus);
}


struct NodeData {
    enum class Kind { Window, Element, Placeholder } kind = Kind::Placeholder;
    HWND hwnd = nullptr;
    IUIAutomationElement* element = nullptr;
    bool loaded = false;
    bool changed = false;
    bool disappeared = false;
    std::wstring title;
    std::wstring displayText;
    std::wstring runtimeId;

    ~NodeData() {
        if (element) element->Release();
    }
};

static std::vector<NodeData*> g_nodes;

static std::wstring BstrToWString(BSTR b) {
    if (!b) return L"";
    return std::wstring(b, SysStringLen(b));
}

static std::wstring RectToString(const RECT& r) {
    std::wstringstream ss;
    ss << L"左=" << r.left << L", 上=" << r.top
       << L", 右=" << r.right << L", 下=" << r.bottom
       << L", 宽=" << (r.right - r.left)
       << L", 高=" << (r.bottom - r.top);
    return ss.str();
}

static std::wstring ControlTypeName(CONTROLTYPEID id) {
    switch (id) {
    case UIA_ButtonControlTypeId: return L"按钮";
    case UIA_CalendarControlTypeId: return L"日历";
    case UIA_CheckBoxControlTypeId: return L"复选框";
    case UIA_ComboBoxControlTypeId: return L"组合框";
    case UIA_EditControlTypeId: return L"编辑框";
    case UIA_HyperlinkControlTypeId: return L"超链接";
    case UIA_ImageControlTypeId: return L"图像";
    case UIA_ListItemControlTypeId: return L"列表项";
    case UIA_ListControlTypeId: return L"列表";
    case UIA_MenuControlTypeId: return L"菜单";
    case UIA_MenuBarControlTypeId: return L"菜单栏";
    case UIA_MenuItemControlTypeId: return L"菜单项";
    case UIA_ProgressBarControlTypeId: return L"进度条";
    case UIA_RadioButtonControlTypeId: return L"单选按钮";
    case UIA_ScrollBarControlTypeId: return L"滚动条";
    case UIA_SliderControlTypeId: return L"滑块";
    case UIA_SpinnerControlTypeId: return L"微调框";
    case UIA_StatusBarControlTypeId: return L"状态栏";
    case UIA_TabControlTypeId: return L"选项卡";
    case UIA_TabItemControlTypeId: return L"选项卡项";
    case UIA_TextControlTypeId: return L"文本";
    case UIA_ToolBarControlTypeId: return L"工具栏";
    case UIA_ToolTipControlTypeId: return L"工具提示";
    case UIA_TreeControlTypeId: return L"树";
    case UIA_TreeItemControlTypeId: return L"树项";
    case UIA_CustomControlTypeId: return L"自定义控件";
    case UIA_GroupControlTypeId: return L"分组";
    case UIA_ThumbControlTypeId: return L"拖动柄";
    case UIA_DataGridControlTypeId: return L"数据网格";
    case UIA_DataItemControlTypeId: return L"数据项";
    case UIA_DocumentControlTypeId: return L"文档";
    case UIA_SplitButtonControlTypeId: return L"拆分按钮";
    case UIA_WindowControlTypeId: return L"窗口";
    case UIA_PaneControlTypeId: return L"面板";
    case UIA_HeaderControlTypeId: return L"表头";
    case UIA_HeaderItemControlTypeId: return L"表头项";
    case UIA_TableControlTypeId: return L"表格";
    case UIA_TitleBarControlTypeId: return L"标题栏";
    case UIA_SeparatorControlTypeId: return L"分隔符";
    case UIA_SemanticZoomControlTypeId: return L"语义缩放";
    case UIA_AppBarControlTypeId: return L"应用栏";
    default: {
        std::wstringstream ss;
        ss << L"未知控件（" << id << L"）";
        return ss.str();
    }
    }
}


static bool IsValidRectForAnnotation(const RECT& r) {
    return r.right > r.left && r.bottom > r.top &&
           (r.right - r.left) > 1 && (r.bottom - r.top) > 1;
}

static LRESULT CALLBACK OverlayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc{};
        GetClientRect(hwnd, &rc);
        FillRect(hdc, &rc, g_hOverlayBrush ? g_hOverlayBrush : (HBRUSH)GetStockObject(DC_BRUSH));
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_NCHITTEST:
        return HTTRANSPARENT;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void RegisterOverlayClass() {
    WNDCLASSW wc{};
    wc.lpfnWndProc = OverlayWndProc;
    wc.hInstance = g_hInst;
    wc.lpszClassName = L"JeriUiaExplorerOverlayBorder";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    RegisterClassW(&wc);
}

static void EnsureOverlayWindows() {
    for (int i = 0; i < 4; ++i) {
        if (g_hOverlay[i] && IsWindow(g_hOverlay[i])) continue;
        g_hOverlay[i] = CreateWindowExW(
            WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT,
            L"JeriUiaExplorerOverlayBorder",
            L"",
            WS_POPUP,
            0, 0, 0, 0,
            nullptr,
            nullptr,
            g_hInst,
            nullptr
        );
    }
}

static void ClearAnnotation() {
    for (HWND h : g_hOverlay) {
        if (h && IsWindow(h)) ShowWindow(h, SW_HIDE);
    }
    g_hasAnnotation = false;
    g_annotatedItem = nullptr;
    g_annotatedNode = nullptr;
    if (g_hStatus) SetStatusText( L"标注已清除。");
}

static void ShowAnnotationRect(const RECT& inputRect) {
    RECT r = inputRect;
    if (!IsValidRectForAnnotation(r)) {
        SetStatusText( L"当前选中项没有可标注的有效区域。");
        return;
    }

    EnsureOverlayWindows();

    const int thickness = 3;
    int width = r.right - r.left;
    int height = r.bottom - r.top;

    MoveWindow(g_hOverlay[0], r.left, r.top, width, thickness, TRUE);                         // top
    MoveWindow(g_hOverlay[1], r.left, r.bottom - thickness, width, thickness, TRUE);          // bottom
    MoveWindow(g_hOverlay[2], r.left, r.top, thickness, height, TRUE);                        // left
    MoveWindow(g_hOverlay[3], r.right - thickness, r.top, thickness, height, TRUE);           // right

    for (HWND h : g_hOverlay) {
        if (!h || !IsWindow(h)) continue;
        SetWindowPos(h, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        InvalidateRect(h, nullptr, TRUE);
    }

    g_hasAnnotation = true;

    std::wstringstream ss;
    ss << L"已标注区域：横坐标=" << r.left
       << L", 纵坐标=" << r.top
       << L", 宽=" << width
       << L", 高=" << height;
    SetStatusText( ss.str().c_str());
}

static bool GetNodeBounds(NodeData* nd, RECT& outRect) {
    if (!nd || nd->disappeared) return false;
    if (nd->kind == NodeData::Kind::Window && nd->hwnd) {
        return GetWindowRect(nd->hwnd, &outRect) && IsValidRectForAnnotation(outRect);
    }
    if (nd->kind == NodeData::Kind::Element && nd->element) {
        HRESULT hr = nd->element->get_CurrentBoundingRectangle(&outRect);
        return SUCCEEDED(hr) && IsValidRectForAnnotation(outRect);
    }
    return false;
}

static void AnnotateNode(NodeData* nd) {
    RECT r{};
    if (!GetNodeBounds(nd, r)) {
        SetStatusText( L"当前选中项没有可标注的有效区域。");
        return;
    }
    ShowAnnotationRect(r);
}

static NodeData* MakeNode(NodeData::Kind kind) {
    NodeData* n = new NodeData();
    n->kind = kind;
    g_nodes.push_back(n);
    return n;
}

static void FreeNodes() {
    g_selectedItems.clear();
    g_searchMatches.clear();
    g_searchScopeItems.clear();
    g_changedItems.clear();
    g_reverseLookupCandidates.clear();

    for (NodeData* n : g_nodes) delete n;
    g_nodes.clear();
}

static void SetNodeElement(NodeData* nd, IUIAutomationElement* element) {
    if (!nd) return;
    if (nd->element == element) return;
    if (nd->element) {
        nd->element->Release();
        nd->element = nullptr;
    }
    nd->element = element;
    if (nd->element) nd->element->AddRef();
}

static void MarkTreeItemChanged(HTREEITEM item) {
    if (!item || !g_hTree) return;

    TVITEMW tv{};
    tv.mask = TVIF_PARAM;
    tv.hItem = item;
    if (!TreeView_GetItem(g_hTree, &tv)) return;

    NodeData* nd = reinterpret_cast<NodeData*>(tv.lParam);
    if (nd && !nd->changed) {
        nd->changed = true;
        g_changedItems.push_back(item);
    }

    RECT rc{};
    if (TreeView_GetItemRect(g_hTree, item, &rc, TRUE)) InvalidateRect(g_hTree, &rc, TRUE);
    else InvalidateRect(g_hTree, nullptr, TRUE);
}

static HTREEITEM InsertTreeItem(HTREEITEM parent, const std::wstring& text, NodeData* data, bool addDummy, bool changed = false) {
    if (data) {
        data->displayText = text;
        data->changed = changed;
    }

    TVINSERTSTRUCTW tvi{};
    tvi.hParent = parent;
    tvi.hInsertAfter = TVI_LAST;
    tvi.item.mask = TVIF_TEXT | TVIF_PARAM;
    tvi.item.pszText = const_cast<LPWSTR>(text.c_str());
    tvi.item.lParam = reinterpret_cast<LPARAM>(data);
    HTREEITEM item = TreeView_InsertItem(g_hTree, &tvi);

    if (addDummy) {
        NodeData* dummy = MakeNode(NodeData::Kind::Placeholder);
        dummy->displayText = L"正在加载……";
        TVINSERTSTRUCTW d{};
        d.hParent = item;
        d.hInsertAfter = TVI_LAST;
        d.item.mask = TVIF_TEXT | TVIF_PARAM;
        d.item.pszText = const_cast<LPWSTR>(L"正在加载……");
        d.item.lParam = reinterpret_cast<LPARAM>(dummy);
        TreeView_InsertItem(g_hTree, &d);
    }
    if (changed) MarkTreeItemChanged(item);
    return item;
}

static NodeData* GetNodeData(HTREEITEM item) {
    if (!item) return nullptr;
    TVITEMW tv{};
    tv.mask = TVIF_PARAM;
    tv.hItem = item;
    if (!TreeView_GetItem(g_hTree, &tv)) return nullptr;
    return reinterpret_cast<NodeData*>(tv.lParam);
}

static bool IsTreeItemAlive(HTREEITEM item) {
    return item && g_hTree && GetNodeData(item) != nullptr;
}

static void AnnotateTreeItem(HTREEITEM item) {
    NodeData* nd = GetNodeData(item);
    if (!item || !nd || nd->kind == NodeData::Kind::Placeholder || nd->disappeared) {
        SetStatusText( nd && nd->disappeared ? L"该节点已消失，无法标注。" : L"当前选中项没有可标注的有效区域。");
        return;
    }
    RECT r{};
    if (!GetNodeBounds(nd, r)) {
        SetStatusText( L"当前选中项没有可标注的有效区域。");
        return;
    }
    g_annotatedItem = item;
    g_annotatedNode = nd;
    ShowAnnotationRect(r);
}

static void RefreshCurrentAnnotation() {
    if (!g_hasAnnotation) return;
    if (!g_annotatedItem || !IsTreeItemAlive(g_annotatedItem)) {
        ClearAnnotation();
        if (g_hStatus) SetStatusText( L"标注目标已消失，已清除标注。");
        return;
    }
    NodeData* nd = GetNodeData(g_annotatedItem);
    if (!nd || nd != g_annotatedNode || nd->kind == NodeData::Kind::Placeholder || nd->disappeared) {
        ClearAnnotation();
        if (g_hStatus) SetStatusText( nd && nd->disappeared ? L"标注目标已消失，已清除标注。" : L"标注目标已变化，已清除标注。");
        return;
    }
    RECT r{};
    if (!GetNodeBounds(nd, r)) {
        ClearAnnotation();
        if (g_hStatus) SetStatusText( L"标注目标暂无有效区域，已清除标注。");
        return;
    }
    ShowAnnotationRect(r);
}

static bool IsSelectedTreeItem(HTREEITEM item) {
    return item && std::find(g_selectedItems.begin(), g_selectedItems.end(), item) != g_selectedItems.end();
}

static void InvalidateTreeItem(HTREEITEM item) {
    if (!item || !g_hTree) return;
    RECT rc{};
    if (TreeView_GetItemRect(g_hTree, item, &rc, TRUE)) {
        InvalidateRect(g_hTree, &rc, TRUE);
    } else {
        InvalidateRect(g_hTree, nullptr, TRUE);
    }
}

static void ClearSelectedItems() {
    std::vector<HTREEITEM> old = g_selectedItems;
    g_selectedItems.clear();
    for (HTREEITEM item : old) InvalidateTreeItem(item);
}

static void SetSingleSelectedItem(HTREEITEM item) {
    ClearSelectedItems();
    NodeData* nd = GetNodeData(item);
    if (item && nd && nd->kind != NodeData::Kind::Placeholder) {
        g_selectedItems.push_back(item);
        InvalidateTreeItem(item);
    }
}

static void ToggleSelectedItem(HTREEITEM item) {
    if (!item) return;
    NodeData* nd = GetNodeData(item);
    if (!nd || nd->kind == NodeData::Kind::Placeholder) return;

    auto it = std::find(g_selectedItems.begin(), g_selectedItems.end(), item);
    if (it == g_selectedItems.end()) {
        g_selectedItems.push_back(item);
    } else {
        g_selectedItems.erase(it);
    }
    InvalidateTreeItem(item);
}

static bool HasSelectedAncestor(HTREEITEM item) {
    HTREEITEM parent = TreeView_GetParent(g_hTree, item);
    while (parent) {
        if (IsSelectedTreeItem(parent)) return true;
        parent = TreeView_GetParent(g_hTree, parent);
    }
    return false;
}


static NodeData* GetSelectedNode() {
    return GetNodeData(TreeView_GetSelection(g_hTree));
}

static bool IsAutoAnnotateChecked() {
    return g_hAutoMark && SendMessageW(g_hAutoMark, BM_GETCHECK, 0, 0) == BST_CHECKED;
}

static void AutoAnnotateSelectedIfNeeded() {
    g_autoAnnotate = IsAutoAnnotateChecked();
    if (!g_autoAnnotate) return;
    AnnotateTreeItem(TreeView_GetSelection(g_hTree));
}

static void RefreshAnnotationAfterTreeRefresh() {
    if (IsAutoAnnotateChecked()) AutoAnnotateSelectedIfNeeded();
    else RefreshCurrentAnnotation();
}

static bool IsOnSplitter(int x) {
    return x >= g_splitterX - 4 && x <= g_splitterX + 8;
}

static int ClampSplitterX(int x, int clientWidth) {
    const int minTreeW = 220;
    const int minDetailW = 260;
    const int margin = 8;
    int minX = margin + minTreeW;
    int maxX = clientWidth - minDetailW - margin * 2;
    if (maxX < minX) maxX = minX;
    if (x < minX) x = minX;
    if (x > maxX) x = maxX;
    return x;
}

static void ExpandAllFromItem(HTREEITEM item, int& count);
static void ExportSingleTreeItem(HTREEITEM item);
static bool NodeSupportsPattern(NodeData* nd, PATTERNID pid, REFIID iid);
static void ExecuteUiaAction(HTREEITEM item, UINT cmd);
static void ExecuteMouseAction(HTREEITEM item, UINT cmd);
static void RefreshWindows();
static void StopReverseLookup(const wchar_t* reason = nullptr);
static void UpdateLookupControlsEnabled();

static void ShowTreeContextMenu() {
    DWORD pos = GetMessagePos();
    POINT screenPt{ GET_X_LPARAM(pos), GET_Y_LPARAM(pos) };
    POINT clientPt = screenPt;
    ScreenToClient(g_hTree, &clientPt);

    TVHITTESTINFO hti{};
    hti.pt = clientPt;
    HTREEITEM item = TreeView_HitTest(g_hTree, &hti);
    if (!item || !(hti.flags & (TVHT_ONITEM | TVHT_ONITEMBUTTON | TVHT_ONITEMICON | TVHT_ONITEMLABEL))) {
        return;
    }

    TreeView_SelectItem(g_hTree, item);
    NodeData* nd = GetNodeData(item);
    if (!nd || nd->kind == NodeData::Kind::Placeholder) return;

    RECT actionRect{};
    bool activeNode = !nd->disappeared;
    bool canMouse = activeNode && GetNodeBounds(nd, actionRect);
    bool canUiaFocus = activeNode && (nd->kind == NodeData::Kind::Element || (nd->kind == NodeData::Kind::Window && nd->hwnd));
    bool canInvoke = NodeSupportsPattern(nd, UIA_InvokePatternId, IID_IUIAutomationInvokePattern);
    bool canToggle = NodeSupportsPattern(nd, UIA_TogglePatternId, IID_IUIAutomationTogglePattern);
    bool canSelect = NodeSupportsPattern(nd, UIA_SelectionItemPatternId, IID_IUIAutomationSelectionItemPattern);
    bool canExpandCollapse = NodeSupportsPattern(nd, UIA_ExpandCollapsePatternId, IID_IUIAutomationExpandCollapsePattern);
    bool canScrollItem = NodeSupportsPattern(nd, UIA_ScrollItemPatternId, IID_IUIAutomationScrollItemPattern);
    bool canWindow = NodeSupportsPattern(nd, UIA_WindowPatternId, IID_IUIAutomationWindowPattern);

    HMENU menu = CreatePopupMenu();
    HMENU uiaMenu = CreatePopupMenu();
    HMENU mouseMenu = CreatePopupMenu();

    AppendMenuW(menu, MF_STRING, IDM_ANNOTATE, L"标注该层级区域");
    AppendMenuW(menu, MF_STRING, IDM_EXPAND_ALL, L"全部展开");
    AppendMenuW(menu, MF_STRING, IDM_EXPORT_THIS, L"导出该项及子项");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

    AppendMenuW(uiaMenu, MF_STRING | (canInvoke ? MF_ENABLED : MF_GRAYED), IDM_UIA_INVOKE, L"执行默认操作 Invoke");
    AppendMenuW(uiaMenu, MF_STRING | (canToggle ? MF_ENABLED : MF_GRAYED), IDM_UIA_TOGGLE, L"切换状态 Toggle");
    AppendMenuW(uiaMenu, MF_STRING | (canSelect ? MF_ENABLED : MF_GRAYED), IDM_UIA_SELECT, L"选择该项 Select");
    AppendMenuW(uiaMenu, MF_STRING | (canExpandCollapse ? MF_ENABLED : MF_GRAYED), IDM_UIA_EXPAND, L"展开 Expand");
    AppendMenuW(uiaMenu, MF_STRING | (canExpandCollapse ? MF_ENABLED : MF_GRAYED), IDM_UIA_COLLAPSE, L"折叠 Collapse");
    AppendMenuW(uiaMenu, MF_STRING | (canScrollItem ? MF_ENABLED : MF_GRAYED), IDM_UIA_SCROLL_INTO_VIEW, L"滚动到可见 ScrollIntoView");
    AppendMenuW(uiaMenu, MF_STRING | (canUiaFocus ? MF_ENABLED : MF_GRAYED), IDM_UIA_SET_FOCUS, L"设置焦点 SetFocus");
    AppendMenuW(uiaMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(uiaMenu, MF_STRING | (canWindow ? MF_ENABLED : MF_GRAYED), IDM_UIA_WINDOW_MINIMIZE, L"最小化窗口");
    AppendMenuW(uiaMenu, MF_STRING | (canWindow ? MF_ENABLED : MF_GRAYED), IDM_UIA_WINDOW_MAXIMIZE, L"最大化窗口");
    AppendMenuW(uiaMenu, MF_STRING | (canWindow ? MF_ENABLED : MF_GRAYED), IDM_UIA_WINDOW_RESTORE, L"还原窗口");
    AppendMenuW(uiaMenu, MF_STRING | (canWindow ? MF_ENABLED : MF_GRAYED), IDM_UIA_WINDOW_CLOSE, L"关闭窗口");

    AppendMenuW(mouseMenu, MF_STRING | (canMouse ? MF_ENABLED : MF_GRAYED), IDM_MOUSE_MOVE_CENTER, L"移动鼠标到元素中心");
    AppendMenuW(mouseMenu, MF_STRING | (canMouse ? MF_ENABLED : MF_GRAYED), IDM_MOUSE_LEFT_CLICK, L"左键单击");
    AppendMenuW(mouseMenu, MF_STRING | (canMouse ? MF_ENABLED : MF_GRAYED), IDM_MOUSE_LEFT_DOUBLE_CLICK, L"左键双击");
    AppendMenuW(mouseMenu, MF_STRING | (canMouse ? MF_ENABLED : MF_GRAYED), IDM_MOUSE_RIGHT_CLICK, L"右键单击");
    AppendMenuW(mouseMenu, MF_STRING | (canMouse ? MF_ENABLED : MF_GRAYED), IDM_MOUSE_DRAG_TO_CURSOR, L"拖动到2秒后鼠标位置");

    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(uiaMenu), L"UIA自动化操作");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(mouseMenu), L"SendInput鼠标模拟");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (g_hasAnnotation ? MF_ENABLED : MF_GRAYED), IDM_CLEAR_ANNOTATION, L"清除标注");

    int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY,
                             screenPt.x, screenPt.y, 0, g_hMain, nullptr);
    DestroyMenu(menu);

    if (cmd == IDM_ANNOTATE) {
        AnnotateTreeItem(item);
    } else if (cmd == IDM_EXPORT_THIS) {
        ExportSingleTreeItem(item);
    } else if (cmd == IDM_EXPAND_ALL) {
        int count = 0;
        SetStatusText( L"正在全部展开，请稍候...");
        {
            ScopedTreeRedraw treeRedraw;
            ExpandAllFromItem(item, count);
        }
        std::wstringstream ss;
        ss << L"全部展开完成，已处理节点数：" << count;
        SetStatusText( ss.str().c_str());
        AutoAnnotateSelectedIfNeeded();
    } else if (cmd == IDM_CLEAR_ANNOTATION) {
        ClearAnnotation();
    } else if (cmd >= IDM_UIA_INVOKE && cmd <= IDM_UIA_WINDOW_RESTORE) {
        ExecuteUiaAction(item, static_cast<UINT>(cmd));
    } else if (cmd >= IDM_MOUSE_MOVE_CENTER && cmd <= IDM_MOUSE_DRAG_TO_CURSOR) {
        ExecuteMouseAction(item, static_cast<UINT>(cmd));
    }
}

static std::wstring ElementText(IUIAutomationElement* e) {
    if (!e) return L"（空元素）";
    BSTR name = nullptr;
    CONTROLTYPEID ct = 0;
    e->get_CurrentName(&name);
    e->get_CurrentControlType(&ct);
    std::wstring n = BstrToWString(name);
    if (name) SysFreeString(name);
    std::wstring type = ControlTypeName(ct);
    if (n.empty()) return L"[" + type + L"]";
    if (n.size() > 80) n = n.substr(0, 80) + L"……";
    return L"[" + type + L"] " + n;
}

static std::wstring ElementRuntimeId(IUIAutomationElement* e) {
    if (!e) return L"";
    SAFEARRAY* arr = nullptr;
    HRESULT hr = e->GetRuntimeId(&arr);
    if (FAILED(hr) || !arr) return L"";

    LONG lb = 0, ub = -1;
    SafeArrayGetLBound(arr, 1, &lb);
    SafeArrayGetUBound(arr, 1, &ub);

    std::wstringstream ss;
    LONG* data = nullptr;
    if (SUCCEEDED(SafeArrayAccessData(arr, reinterpret_cast<void**>(&data))) && data) {
        for (LONG i = lb; i <= ub; ++i) {
            if (i > lb) ss << L".";
            ss << data[i - lb];
        }
        SafeArrayUnaccessData(arr);
    }
    SafeArrayDestroy(arr);
    return ss.str();
}

static bool ElementHasChildren(IUIAutomationElement* e) {
    if (!e) return false;
    IUIAutomationTreeWalker* walker = GetControlViewWalker();
    if (!walker) return false;
    IUIAutomationElement* child = nullptr;
    HRESULT hr = walker->GetFirstChildElement(e, &child);
    if (SUCCEEDED(hr) && child) {
        child->Release();
        return true;
    }
    return false;
}

static bool IsTreeItemSameOrChildOf(HTREEITEM item, HTREEITEM parent) {
    while (item) {
        if (item == parent) return true;
        item = TreeView_GetParent(g_hTree, item);
    }
    return false;
}

static void RemoveSelectedUnder(HTREEITEM item) {
    if (!item) return;
    g_selectedItems.erase(
        std::remove_if(g_selectedItems.begin(), g_selectedItems.end(),
            [item](HTREEITEM selected) { return IsTreeItemSameOrChildOf(selected, item); }),
        g_selectedItems.end());
}

static void RemoveChangedUnder(HTREEITEM item) {
    if (!item) return;
    g_changedItems.erase(
        std::remove_if(g_changedItems.begin(), g_changedItems.end(),
            [item](HTREEITEM changed) { return IsTreeItemSameOrChildOf(changed, item); }),
        g_changedItems.end());
}

static void DeleteTreeItemSafe(HTREEITEM item) {
    if (!item) return;
    if (g_annotatedItem && IsTreeItemSameOrChildOf(g_annotatedItem, item)) {
        ClearAnnotation();
    }
    if (g_reverseLookupWindowItem && IsTreeItemSameOrChildOf(g_reverseLookupWindowItem, item)) {
        StopReverseLookup(L"反找目标窗口已移除，已退出鼠标反找。");
    }
    if (g_reverseLookupLastItem && IsTreeItemSameOrChildOf(g_reverseLookupLastItem, item)) {
        g_reverseLookupLastItem = nullptr;
    }
    RemoveSelectedUnder(item);
    RemoveChangedUnder(item);
    g_reverseLookupCandidates.clear();
    TreeView_DeleteItem(g_hTree, item);
}

static void DeleteChildren(HTREEITEM item) {
    HTREEITEM child = TreeView_GetChild(g_hTree, item);
    while (child) {
        DeleteTreeItemSafe(child);
        child = TreeView_GetChild(g_hTree, item);
    }
}

static void LoadElementChildren(HTREEITEM parentItem, IUIAutomationElement* parentElem) {
    if (!g_uia || !parentElem) return;
    DeleteChildren(parentItem);

    IUIAutomationTreeWalker* walker = GetControlViewWalker();
    if (!walker) return;

    int count = 0;
    IUIAutomationElement* child = nullptr;
    HRESULT hr = walker->GetFirstChildElement(parentElem, &child);
    while (SUCCEEDED(hr) && child) {
        NodeData* nd = MakeNode(NodeData::Kind::Element);
        SetNodeElement(nd, child);
        nd->runtimeId = ElementRuntimeId(child);
        std::wstring text = ElementText(child);
        bool hasChild = ElementHasChildren(child);
        InsertTreeItem(parentItem, text, nd, hasChild);
        count++;

        IUIAutomationElement* next = nullptr;
        hr = walker->GetNextSiblingElement(child, &next);
        child->Release();
        child = next;
    }

    if (count == 0) {
        NodeData* empty = MakeNode(NodeData::Kind::Placeholder);
        InsertTreeItem(parentItem, L"（无子元素）", empty, false);
    }
}

static void LoadWindowChildren(HTREEITEM item, NodeData* nd) {
    if (!g_uia || !nd || !nd->hwnd) return;
    DeleteChildren(item);

    IUIAutomationElement* root = nullptr;
    HRESULT hr = g_uia->ElementFromHandle(nd->hwnd, &root);
    if (FAILED(hr) || !root) {
        NodeData* err = MakeNode(NodeData::Kind::Placeholder);
        InsertTreeItem(item, L"（无法读取窗口元素）", err, false);
        return;
    }

    NodeData* rootNode = MakeNode(NodeData::Kind::Element);
    SetNodeElement(rootNode, root);
    rootNode->runtimeId = ElementRuntimeId(root);
    HTREEITEM rootItem = InsertTreeItem(item, ElementText(root), rootNode, ElementHasChildren(root));
    TreeView_Expand(g_hTree, rootItem, TVE_EXPAND);
    root->Release();
}

static void EnsureItemLoaded(HTREEITEM item) {
    NodeData* nd = GetNodeData(item);
    if (!nd || nd->loaded || nd->kind == NodeData::Kind::Placeholder || nd->disappeared) return;

    nd->loaded = true;
    if (nd->kind == NodeData::Kind::Window) {
        LoadWindowChildren(item, nd);
    } else if (nd->kind == NodeData::Kind::Element) {
        LoadElementChildren(item, nd->element);
    }
}

static void ExpandAllFromItem(HTREEITEM item, int& count) {
    if (!item || count >= MAX_EXPAND_ALL_NODES) return;

    NodeData* nd = GetNodeData(item);
    if (!nd || nd->kind == NodeData::Kind::Placeholder) return;

    ++count;
    EnsureItemLoaded(item);
    TreeView_Expand(g_hTree, item, TVE_EXPAND);

    HTREEITEM child = TreeView_GetChild(g_hTree, item);
    while (child && count < MAX_EXPAND_ALL_NODES) {
        HTREEITEM next = TreeView_GetNextSibling(g_hTree, child);
        ExpandAllFromItem(child, count);
        child = next;
    }
}

static std::wstring GetTreeItemText(HTREEITEM item) {
    wchar_t text[512]{};
    TVITEMW tv{};
    tv.mask = TVIF_TEXT;
    tv.hItem = item;
    tv.pszText = text;
    tv.cchTextMax = 512;
    if (!TreeView_GetItem(g_hTree, &tv)) return L"";
    return text;
}

static std::wstring GetTreeItemDisplayText(HTREEITEM item) {
    NodeData* nd = GetNodeData(item);
    if (nd && !nd->displayText.empty()) return nd->displayText;
    return GetTreeItemText(item);
}

static void ClearSearchResults() {
    g_lastSearchKeyword.clear();
    g_searchMatches.clear();
    g_searchIndex = -1;
    g_searchScopeItems.clear();
}

static bool IsPausedToolControlEnabled() {
    return g_refreshPaused && !g_isRefreshing;
}

static bool IsSearchControlEnabled() {
    return IsPausedToolControlEnabled() && !g_reverseLookupActive;
}

static bool IsReverseLookupControlEnabled() {
    return IsPausedToolControlEnabled();
}

static void UpdateReverseLookupButtonText() {
    SetWindowTextIfChanged(g_hReverseLookup, g_reverseLookupActive ? L"停止反找" : L"鼠标反找");
}

static void UpdateLookupControlsEnabled() {
    BOOL searchEnabled = IsSearchControlEnabled() ? TRUE : FALSE;
    SetWindowEnabledIfChanged(g_hSearchEdit, searchEnabled);
    SetWindowEnabledIfChanged(g_hSearchButton, searchEnabled);
    SetWindowEnabledIfChanged(g_hSearchPrev, searchEnabled);
    SetWindowEnabledIfChanged(g_hSearchNext, searchEnabled);

    BOOL reverseEnabled = IsReverseLookupControlEnabled() ? TRUE : FALSE;
    SetWindowEnabledIfChanged(g_hReverseLookup, reverseEnabled);
    UpdateReverseLookupButtonText();
}

static void UpdateSearchControlsEnabled() {
    UpdateLookupControlsEnabled();
}

struct RefreshSearchGuard {
    RefreshSearchGuard() {
        g_isRefreshing = true;
        ClearSearchResults();
        UpdateSearchControlsEnabled();
    }
    ~RefreshSearchGuard() {
        g_isRefreshing = false;
        UpdateSearchControlsEnabled();
    }
};

static std::wstring TrimSearchText(const std::wstring& value) {
    size_t begin = 0;
    size_t end = value.size();
    while (begin < end && iswspace(value[begin])) ++begin;
    while (end > begin && iswspace(value[end - 1])) --end;
    return value.substr(begin, end - begin);
}

static std::wstring NormalizeSearchText(const std::wstring& value) {
    std::wstring out;
    out.reserve(value.size());
    for (wchar_t ch : value) {
        if (iswspace(ch)) continue;
        out.push_back(static_cast<wchar_t>(towlower(ch)));
    }
    return out;
}

static bool FuzzyTextMatch(const std::wstring& text, const std::wstring& keyword) {
    std::wstring target = NormalizeSearchText(text);
    std::wstring pattern = NormalizeSearchText(keyword);
    if (pattern.empty() || target.empty()) return false;
    if (target.find(pattern) != std::wstring::npos) return true;

    size_t pos = 0;
    for (wchar_t ch : pattern) {
        pos = target.find(ch, pos);
        if (pos == std::wstring::npos) return false;
        ++pos;
    }
    return true;
}

static std::wstring GetSearchKeywordFromEdit() {
    if (!g_hSearchEdit) return L"";
    int len = GetWindowTextLengthW(g_hSearchEdit);
    if (len <= 0) return L"";
    std::wstring text(static_cast<size_t>(len + 1), L'\0');
    GetWindowTextW(g_hSearchEdit, &text[0], len + 1);
    text.resize(static_cast<size_t>(len));
    return TrimSearchText(text);
}

static void AddSearchScopeItem(std::vector<HTREEITEM>& list, HTREEITEM item) {
    if (!item || !IsTreeItemAlive(item)) return;
    NodeData* nd = GetNodeData(item);
    if (!nd || nd->kind == NodeData::Kind::Placeholder) return;

    for (HTREEITEM existing : list) {
        if (existing == item || IsTreeItemSameOrChildOf(item, existing)) return;
    }

    list.erase(
        std::remove_if(list.begin(), list.end(),
            [item](HTREEITEM existing) { return IsTreeItemSameOrChildOf(existing, item); }),
        list.end());
    list.push_back(item);
}

static std::vector<HTREEITEM> GetSearchScopeItems() {
    std::vector<HTREEITEM> scopes;
    for (HTREEITEM item : g_selectedItems) {
        AddSearchScopeItem(scopes, item);
    }
    return scopes;
}

static bool HasSearchScope() {
    return !g_searchScopeItems.empty();
}

static void EnsureSearchChildrenLoaded(HTREEITEM item) {
    NodeData* nd = GetNodeData(item);
    if (!nd || nd->kind == NodeData::Kind::Placeholder || nd->disappeared) return;
    if (!nd->loaded) EnsureItemLoaded(item);
}

static bool CollectSearchMatchesInSubtree(HTREEITEM item, const std::wstring& keyword, std::vector<HTREEITEM>& matches, int& visited) {
    if (!item || !g_hTree || visited >= MAX_SEARCH_NODES) return visited < MAX_SEARCH_NODES;

    NodeData* nd = GetNodeData(item);
    if (!nd || nd->kind == NodeData::Kind::Placeholder || nd->disappeared) return true;

    ++visited;
    EnsureSearchChildrenLoaded(item);

    if (FuzzyTextMatch(GetTreeItemDisplayText(item), keyword)) {
        matches.push_back(item);
    }

    HTREEITEM child = TreeView_GetChild(g_hTree, item);
    while (child && visited < MAX_SEARCH_NODES) {
        HTREEITEM next = TreeView_GetNextSibling(g_hTree, child);
        if (!CollectSearchMatchesInSubtree(child, keyword, matches, visited)) return false;
        child = next;
    }
    return visited < MAX_SEARCH_NODES;
}

static bool CollectSearchMatchesFromRoots(HTREEITEM item, const std::wstring& keyword, std::vector<HTREEITEM>& matches, int& visited) {
    while (item && visited < MAX_SEARCH_NODES) {
        HTREEITEM next = TreeView_GetNextSibling(g_hTree, item);
        if (!CollectSearchMatchesInSubtree(item, keyword, matches, visited)) return false;
        item = next;
    }
    return visited < MAX_SEARCH_NODES;
}

static void ExpandParentsForSearchItem(HTREEITEM item) {
    std::vector<HTREEITEM> parents;
    HTREEITEM parent = TreeView_GetParent(g_hTree, item);
    while (parent) {
        parents.push_back(parent);
        parent = TreeView_GetParent(g_hTree, parent);
    }
    for (auto it = parents.rbegin(); it != parents.rend(); ++it) {
        TreeView_Expand(g_hTree, *it, TVE_EXPAND);
    }
}

static void SelectSearchResult(int index) {
    if (index < 0 || index >= static_cast<int>(g_searchMatches.size())) return;
    HTREEITEM item = g_searchMatches[static_cast<size_t>(index)];
    if (!IsTreeItemAlive(item)) {
        SetStatusText(L"搜索结果已变化，请重新搜索。");
        ClearSearchResults();
        return;
    }

    ExpandParentsForSearchItem(item);
    g_searchIndex = index;
    TreeView_SelectItem(g_hTree, item);
    TreeView_EnsureVisible(g_hTree, item);
    SetFocus(g_hTree);

    std::wstringstream ss;
    ss << L"找到匹配项 " << (g_searchIndex + 1) << L"/" << g_searchMatches.size()
       << L"：" << GetTreeItemDisplayText(item);
    SetStatusText(ss.str().c_str());
}

static bool RebuildSearchMatches(const std::wstring& keyword) {
    ClearSearchResults();
    g_lastSearchKeyword = keyword;
    g_searchScopeItems = GetSearchScopeItems();

    SetStatusText(HasSearchScope() ? L"正在搜索选中项及其子项……" : L"正在搜索全部窗口及其节点……");
    UpdateWindow(g_hStatus);

    int visited = 0;
    bool complete = true;
    {
        ScopedTreeRedraw treeRedraw;
        if (HasSearchScope()) {
            for (HTREEITEM item : g_searchScopeItems) {
                if (!CollectSearchMatchesInSubtree(item, keyword, g_searchMatches, visited)) {
                    complete = false;
                    break;
                }
            }
        } else {
            complete = CollectSearchMatchesFromRoots(TreeView_GetRoot(g_hTree), keyword, g_searchMatches, visited);
        }
    }

    if (!complete) {
        SetStatusText(L"搜索已达到10000节点上限，已返回当前匹配结果。");
    }
    return !g_searchMatches.empty();
}

static void SearchFromCurrentSelection() {
    if (!IsSearchControlEnabled()) {
        SetStatusText(L"自动刷新期间禁止搜索，请先点击“暂停刷新”。");
        return;
    }

    std::wstring keyword = GetSearchKeywordFromEdit();
    if (keyword.empty()) {
        SetStatusText(L"请输入搜索关键字。");
        return;
    }

    if (!RebuildSearchMatches(keyword)) {
        SetStatusText(HasSearchScope() ? L"选中项及其子项中未找到匹配项。" : L"全部窗口及节点中未找到匹配项。");
        return;
    }
    SelectSearchResult(0);
}

static bool EnsureSearchMatchesReady(const std::wstring& keyword) {
    if (!g_searchMatches.empty() && keyword == g_lastSearchKeyword) return true;
    if (!RebuildSearchMatches(keyword)) {
        SetStatusText(HasSearchScope() ? L"选中项及其子项中未找到匹配项。" : L"全部窗口及节点中未找到匹配项。");
        return false;
    }
    SelectSearchResult(0);
    return false;
}

static void MoveSearchResult(int step) {
    if (!IsSearchControlEnabled()) {
        SetStatusText(L"自动刷新期间禁止搜索，请先点击“暂停刷新”。");
        return;
    }

    std::wstring keyword = GetSearchKeywordFromEdit();
    if (keyword.empty()) {
        SetStatusText(L"请输入搜索关键字。");
        return;
    }

    if (!EnsureSearchMatchesReady(keyword)) return;

    HTREEITEM selected = TreeView_GetSelection(g_hTree);
    int current = g_searchIndex;
    auto it = std::find(g_searchMatches.begin(), g_searchMatches.end(), selected);
    if (it != g_searchMatches.end()) {
        current = static_cast<int>(std::distance(g_searchMatches.begin(), it));
    }

    int next = current + step;
    if (next < 0) {
        SetStatusText(L"没有上一项匹配结果。");
        return;
    }
    if (next >= static_cast<int>(g_searchMatches.size())) {
        SetStatusText(L"没有下一项匹配结果。");
        return;
    }
    SelectSearchResult(next);
}

static void SearchPreviousResult() {
    MoveSearchResult(-1);
}

static void SearchNextResult() {
    MoveSearchResult(1);
}


static bool IsPointInScreenRect(const POINT& pt, const RECT& rc) {
    return pt.x >= rc.left && pt.x < rc.right && pt.y >= rc.top && pt.y < rc.bottom;
}

static long long RectAreaValue(const RECT& rc) {
    long long w = static_cast<long long>(rc.right) - rc.left;
    long long h = static_cast<long long>(rc.bottom) - rc.top;
    if (w < 0) w = 0;
    if (h < 0) h = 0;
    return w * h;
}

static HTREEITEM FindWindowAncestorItem(HTREEITEM item) {
    while (item) {
        NodeData* nd = GetNodeData(item);
        if (nd && nd->kind == NodeData::Kind::Window) return item;
        item = TreeView_GetParent(g_hTree, item);
    }
    return nullptr;
}

static HTREEITEM GetReverseLookupWindowFromSelection() {
    HTREEITEM current = TreeView_GetSelection(g_hTree);
    if (IsSelectedTreeItem(current)) {
        HTREEITEM windowItem = FindWindowAncestorItem(current);
        if (windowItem) return windowItem;
    }

    for (HTREEITEM item : g_selectedItems) {
        HTREEITEM windowItem = FindWindowAncestorItem(item);
        if (windowItem) return windowItem;
    }
    return nullptr;
}

static bool IsReverseLookupWindowValid() {
    if (!g_reverseLookupWindowItem) return false;
    NodeData* nd = GetNodeData(g_reverseLookupWindowItem);
    if (!nd || nd->kind != NodeData::Kind::Window || nd->disappeared) return false;
    return nd->hwnd && IsWindow(nd->hwnd) && IsWindowVisible(nd->hwnd);
}

static void SetReverseLookupStatus(const wchar_t* text) {
    std::wstring value = text ? text : L"";
    if (value == g_reverseLookupLastStatus) return;
    g_reverseLookupLastStatus = value;
    SetStatusText(value.c_str());
}

static void CollectReverseLookupCandidates(HTREEITEM item, int depth, int& visited) {
    if (!item || depth > MAX_REVERSE_LOOKUP_DEPTH || visited >= MAX_REVERSE_LOOKUP_NODES) return;
    ++visited;

    NodeData* nd = GetNodeData(item);
    if (!nd || nd->kind == NodeData::Kind::Placeholder || nd->disappeared) return;

    RECT rc{};
    if (GetNodeBounds(nd, rc)) {
        ReverseLookupCandidate candidate;
        candidate.item = item;
        candidate.rect = rc;
        candidate.depth = depth;
        candidate.area = RectAreaValue(rc);
        g_reverseLookupCandidates.push_back(candidate);
    }

    if (!nd->loaded && depth > 0) return;

    HTREEITEM child = TreeView_GetChild(g_hTree, item);
    while (child && visited < MAX_REVERSE_LOOKUP_NODES) {
        HTREEITEM next = TreeView_GetNextSibling(g_hTree, child);
        CollectReverseLookupCandidates(child, depth + 1, visited);
        child = next;
    }
}

static void RebuildReverseLookupCandidates() {
    g_reverseLookupCandidates.clear();
    if (!IsReverseLookupWindowValid()) return;

    int visited = 0;
    CollectReverseLookupCandidates(g_reverseLookupWindowItem, 0, visited);
    std::sort(g_reverseLookupCandidates.begin(), g_reverseLookupCandidates.end(),
        [](const ReverseLookupCandidate& a, const ReverseLookupCandidate& b) {
            if (a.depth != b.depth) return a.depth > b.depth;
            if (a.area != b.area) return a.area < b.area;
            return a.item < b.item;
        });
}

static HTREEITEM FindCachedReverseLookupHit(const POINT& pt) {
    for (const ReverseLookupCandidate& candidate : g_reverseLookupCandidates) {
        if (candidate.item && IsPointInScreenRect(pt, candidate.rect)) return candidate.item;
    }
    return nullptr;
}

static HTREEITEM FindReverseLookupHitFast(const POINT& pt) {
    HTREEITEM hit = FindCachedReverseLookupHit(pt);
    for (int i = 0; i < 3 && hit; ++i) {
        NodeData* nd = GetNodeData(hit);
        if (!nd || nd->loaded || nd->kind == NodeData::Kind::Placeholder || nd->disappeared) break;
        EnsureItemLoaded(hit);
        RebuildReverseLookupCandidates();
        HTREEITEM deeper = FindCachedReverseLookupHit(pt);
        if (!deeper || deeper == hit) break;
        hit = deeper;
    }
    return hit;
}

static void SelectReverseLookupItem(HTREEITEM item) {
    NodeData* nd = GetNodeData(item);
    if (!item || !nd || nd->kind == NodeData::Kind::Placeholder || nd->disappeared) return;
    if (item == g_reverseLookupLastItem && item == TreeView_GetSelection(g_hTree)) return;

    ExpandParentsForSearchItem(item);
    g_reverseLookupLastItem = item;
    TreeView_SelectItem(g_hTree, item);
    TreeView_EnsureVisible(g_hTree, item);

    std::wstringstream ss;
    ss << L"鼠标反找命中：" << GetTreeItemDisplayText(item) << L"。请返回本工具点击“停止反找”。";
    SetReverseLookupStatus(ss.str().c_str());
}

static void TrackReverseLookupMouse() {
    if (!g_reverseLookupActive) return;
    if (!IsReverseLookupWindowValid()) {
        StopReverseLookup(L"反找目标窗口不可用，已退出鼠标反找。");
        return;
    }

    POINT pt{};
    if (!GetCursorPos(&pt)) return;
    if (pt.x == g_reverseLookupLastPoint.x && pt.y == g_reverseLookupLastPoint.y) return;
    g_reverseLookupLastPoint = pt;

    NodeData* winNode = GetNodeData(g_reverseLookupWindowItem);
    RECT winRect{};
    if (!GetNodeBounds(winNode, winRect) || !IsPointInScreenRect(pt, winRect)) {
        SetReverseLookupStatus(L"鼠标反找中：鼠标不在目标窗口范围内。请返回本工具点击“停止反找”。");
        return;
    }

    if (g_reverseLookupCandidates.empty()) RebuildReverseLookupCandidates();
    HTREEITEM hit = FindReverseLookupHitFast(pt);
    if (hit) {
        SelectReverseLookupItem(hit);
    } else {
        SetReverseLookupStatus(L"鼠标反找中：当前坐标未匹配到可选元素。请返回本工具点击“停止反找”。");
    }
}

static void StopReverseLookup(const wchar_t* reason) {
    if (!g_reverseLookupActive) return;
    KillTimer(g_hMain, IDT_REVERSE_LOOKUP);
    g_reverseLookupActive = false;
    g_reverseLookupWindowItem = nullptr;
    g_reverseLookupLastItem = nullptr;
    g_reverseLookupCandidates.clear();
    g_reverseLookupLastPoint = POINT{ LONG_MIN, LONG_MIN };
    g_reverseLookupLastStatus.clear();
    UpdateLookupControlsEnabled();
    if (reason && reason[0]) SetStatusText(reason);
    else SetStatusText(L"已退出鼠标反找。");
}

static void StartReverseLookup(HWND hwnd) {
    if (!IsReverseLookupControlEnabled()) {
        SetStatusText(L"自动刷新期间禁止反找，请先点击“暂停刷新”。");
        return;
    }

    HTREEITEM windowItem = GetReverseLookupWindowFromSelection();
    if (!windowItem) {
        MessageBoxW(hwnd, L"请先在左侧树中选中一个窗口，或选中该窗口下的任意子节点。", L"鼠标反找", MB_ICONINFORMATION);
        SetStatusText(L"请先选中一个窗口或窗口下的节点后再反找。");
        return;
    }

    NodeData* winNode = GetNodeData(windowItem);
    if (!winNode || winNode->kind != NodeData::Kind::Window || winNode->disappeared || !winNode->hwnd || !IsWindow(winNode->hwnd)) {
        MessageBoxW(hwnd, L"当前选中的窗口不可用，请重新选择一个有效窗口。", L"鼠标反找", MB_ICONWARNING);
        return;
    }

    MessageBoxW(hwnd,
                L"现在将会持续跟踪您的鼠标，并以您的鼠标为中心自动反找。\r\n\r\n如需要视觉反馈请勾选“预览时标注”。\r\n\r\n请返回本工具点击“停止反找”结束反找。",
                L"鼠标反找",
                MB_ICONINFORMATION);

    ClearSearchResults();
    g_reverseLookupActive = true;
    g_reverseLookupWindowItem = windowItem;
    g_reverseLookupLastItem = nullptr;
    g_reverseLookupCandidates.clear();
    g_reverseLookupLastPoint = POINT{ LONG_MIN, LONG_MIN };
    g_reverseLookupLastStatus.clear();
    EnsureItemLoaded(g_reverseLookupWindowItem);
    RebuildReverseLookupCandidates();
    SetTimer(hwnd, IDT_REVERSE_LOOKUP, REVERSE_LOOKUP_TIMER_MS, nullptr);
    UpdateLookupControlsEnabled();
    SetReverseLookupStatus(L"鼠标反找中：移动鼠标自动匹配元素，请返回本工具点击“停止反找”。");
    TrackReverseLookupMouse();
}

static void ToggleReverseLookup(HWND hwnd) {
    if (g_reverseLookupActive) {
        StopReverseLookup(L"已退出鼠标反找。");
    } else {
        StartReverseLookup(hwnd);
    }
}

static bool IsExportableTreeItem(HTREEITEM item) {
    NodeData* nd = GetNodeData(item);
    return item && nd && nd->kind != NodeData::Kind::Placeholder;
}

static void JsonIndent(std::wstringstream& ss, int depth) {
    for (int i = 0; i < depth; ++i) ss << L"  ";
}

static std::wstring JsonEscape(const std::wstring& value) {
    std::wstringstream out;
    const wchar_t* hex = L"0123456789ABCDEF";
    for (wchar_t ch : value) {
        switch (ch) {
        case L'\\': out << L"\\\\"; break;
        case L'\"': out << L"\\\""; break;
        case L'\b': out << L"\\b"; break;
        case L'\f': out << L"\\f"; break;
        case L'\n': out << L"\\n"; break;
        case L'\r': out << L"\\r"; break;
        case L'\t': out << L"\\t"; break;
        default:
            if (ch < 0x20) {
                unsigned int code = static_cast<unsigned int>(ch);
                out << L"\\u"
                    << hex[(code >> 12) & 0xF]
                    << hex[(code >> 8) & 0xF]
                    << hex[(code >> 4) & 0xF]
                    << hex[code & 0xF];
            } else {
                out << ch;
            }
            break;
        }
    }
    return out.str();
}

static std::wstring HwndToJsonString(HWND hwnd) {
    std::wstringstream ss;
    ss << L"0x" << std::hex << std::uppercase << reinterpret_cast<UINT_PTR>(hwnd);
    return ss.str();
}

static void JsonStringField(std::wstringstream& ss, int depth, const wchar_t* key, const std::wstring& value, bool comma = true) {
    JsonIndent(ss, depth);
    ss << L"\"" << key << L"\": \"" << JsonEscape(value) << L"\"";
    if (comma) ss << L",";
    ss << L"\r\n";
}

static void JsonNumberField(std::wstringstream& ss, int depth, const wchar_t* key, long long value, bool comma = true) {
    JsonIndent(ss, depth);
    ss << L"\"" << key << L"\": " << value;
    if (comma) ss << L",";
    ss << L"\r\n";
}

static void JsonBoolField(std::wstringstream& ss, int depth, const wchar_t* key, bool value, bool comma = true) {
    JsonIndent(ss, depth);
    ss << L"\"" << key << L"\": " << (value ? L"true" : L"false");
    if (comma) ss << L",";
    ss << L"\r\n";
}

static void JsonRectField(std::wstringstream& ss, int depth, const wchar_t* key, const RECT& r, bool comma = true) {
    JsonIndent(ss, depth);
    ss << L"\"" << key << L"\": {";
    ss << L"\"left\": " << r.left << L", ";
    ss << L"\"top\": " << r.top << L", ";
    ss << L"\"right\": " << r.right << L", ";
    ss << L"\"bottom\": " << r.bottom << L", ";
    ss << L"\"width\": " << (r.right - r.left) << L", ";
    ss << L"\"height\": " << (r.bottom - r.top) << L"}";
    if (comma) ss << L",";
    ss << L"\r\n";
}

static void AppendPatternJson(IUIAutomationElement* e, PATTERNID pid, REFIID iid, const wchar_t* name, int depth, std::wstringstream& ss, bool& first) {
    IUnknown* p = nullptr;
    HRESULT hr = e->GetCurrentPatternAs(pid, iid, reinterpret_cast<void**>(&p));
    if (SUCCEEDED(hr) && p) {
        if (!first) ss << L",\r\n";
        JsonIndent(ss, depth);
        ss << L"\"" << JsonEscape(name) << L"\"";
        first = false;
        p->Release();
    }
}

static void JsonPatternArrayField(std::wstringstream& ss, int depth, IUIAutomationElement* e) {
    JsonIndent(ss, depth);
    ss << L"\"patterns\": [";
    bool first = true;
    if (e) {
        AppendPatternJson(e, UIA_InvokePatternId, IID_IUIAutomationInvokePattern, L"调用模式：可点击或可调用", depth + 1, ss, first);
        AppendPatternJson(e, UIA_ValuePatternId, IID_IUIAutomationValuePattern, L"取值模式：可读取或输入值", depth + 1, ss, first);
        AppendPatternJson(e, UIA_TextPatternId, IID_IUIAutomationTextPattern, L"文本模式：提供文本内容", depth + 1, ss, first);
        AppendPatternJson(e, UIA_TogglePatternId, IID_IUIAutomationTogglePattern, L"切换模式：复选框或开关", depth + 1, ss, first);
        AppendPatternJson(e, UIA_SelectionItemPatternId, IID_IUIAutomationSelectionItemPattern, L"选择项模式：可选择项目", depth + 1, ss, first);
        AppendPatternJson(e, UIA_ExpandCollapsePatternId, IID_IUIAutomationExpandCollapsePattern, L"展开折叠模式：可展开或收起", depth + 1, ss, first);
        AppendPatternJson(e, UIA_RangeValuePatternId, IID_IUIAutomationRangeValuePattern, L"范围取值模式：滑块或范围值", depth + 1, ss, first);
        AppendPatternJson(e, UIA_ScrollPatternId, IID_IUIAutomationScrollPattern, L"滚动模式：可滚动区域", depth + 1, ss, first);
        AppendPatternJson(e, UIA_ScrollItemPatternId, IID_IUIAutomationScrollItemPattern, L"滚动项模式：可滚动到该项目", depth + 1, ss, first);
        AppendPatternJson(e, UIA_WindowPatternId, IID_IUIAutomationWindowPattern, L"窗口模式：窗口控制能力", depth + 1, ss, first);
        AppendPatternJson(e, UIA_DockPatternId, IID_IUIAutomationDockPattern, L"停靠模式", depth + 1, ss, first);
        AppendPatternJson(e, UIA_GridPatternId, IID_IUIAutomationGridPattern, L"网格模式", depth + 1, ss, first);
        AppendPatternJson(e, UIA_GridItemPatternId, IID_IUIAutomationGridItemPattern, L"网格项模式", depth + 1, ss, first);
        AppendPatternJson(e, UIA_TablePatternId, IID_IUIAutomationTablePattern, L"表格模式", depth + 1, ss, first);
        AppendPatternJson(e, UIA_TableItemPatternId, IID_IUIAutomationTableItemPattern, L"表格项模式", depth + 1, ss, first);
    }
    if (!first) {
        ss << L"\r\n";
        JsonIndent(ss, depth);
    }
    ss << L"],\r\n";
}

static void ExportElementJsonFields(IUIAutomationElement* e, int depth, std::wstringstream& ss, const std::wstring& displayText) {
    BSTR name = nullptr, aid = nullptr, cls = nullptr, fw = nullptr;
    CONTROLTYPEID ct = 0;
    RECT rect{};
    BOOL enabled = FALSE, offscreen = FALSE, focusable = FALSE, focused = FALSE;
    int pid = 0;

    if (e) {
        e->get_CurrentName(&name);
        e->get_CurrentAutomationId(&aid);
        e->get_CurrentClassName(&cls);
        e->get_CurrentFrameworkId(&fw);
        e->get_CurrentControlType(&ct);
        e->get_CurrentBoundingRectangle(&rect);
        e->get_CurrentIsEnabled(&enabled);
        e->get_CurrentIsOffscreen(&offscreen);
        e->get_CurrentIsKeyboardFocusable(&focusable);
        e->get_CurrentHasKeyboardFocus(&focused);
        e->get_CurrentProcessId(&pid);
    }

    JsonStringField(ss, depth, L"kind", L"element");
    JsonStringField(ss, depth, L"displayText", displayText);
    JsonStringField(ss, depth, L"name", BstrToWString(name));
    JsonStringField(ss, depth, L"controlTypeName", ControlTypeName(ct));
    JsonNumberField(ss, depth, L"controlTypeId", static_cast<long long>(ct));
    JsonStringField(ss, depth, L"automationId", BstrToWString(aid));
    JsonStringField(ss, depth, L"className", BstrToWString(cls));
    JsonStringField(ss, depth, L"frameworkId", BstrToWString(fw));
    JsonNumberField(ss, depth, L"processId", static_cast<long long>(pid));
    JsonRectField(ss, depth, L"bounds", rect);
    JsonBoolField(ss, depth, L"isEnabled", enabled == TRUE);
    JsonBoolField(ss, depth, L"isOffscreen", offscreen == TRUE);
    JsonBoolField(ss, depth, L"isKeyboardFocusable", focusable == TRUE);
    JsonBoolField(ss, depth, L"hasKeyboardFocus", focused == TRUE);
    JsonPatternArrayField(ss, depth, e);

    if (name) SysFreeString(name);
    if (aid) SysFreeString(aid);
    if (cls) SysFreeString(cls);
    if (fw) SysFreeString(fw);
}

static bool ExportTreeItemJsonRecursive(HTREEITEM item, int depth, std::wstringstream& ss, int& count) {
    if (!IsExportableTreeItem(item) || count >= 10000) return false;

    NodeData* nd = GetNodeData(item);
    std::wstring displayText = GetTreeItemText(item);
    ++count;

    JsonIndent(ss, depth);
    ss << L"{\r\n";

    if (nd->kind == NodeData::Kind::Window) {
        wchar_t cls[256]{};
        DWORD pid = 0;
        RECT r{};
        if (!nd->disappeared && nd->hwnd && IsWindow(nd->hwnd)) {
            GetClassNameW(nd->hwnd, cls, 256);
            GetWindowThreadProcessId(nd->hwnd, &pid);
            GetWindowRect(nd->hwnd, &r);
        }

        JsonStringField(ss, depth + 1, L"kind", L"window");
        JsonStringField(ss, depth + 1, L"displayText", displayText);
        JsonBoolField(ss, depth + 1, L"disappeared", nd->disappeared);
        JsonStringField(ss, depth + 1, L"title", nd->title);
        JsonStringField(ss, depth + 1, L"hwnd", HwndToJsonString(nd->hwnd));
        JsonStringField(ss, depth + 1, L"className", cls);
        JsonNumberField(ss, depth + 1, L"processId", static_cast<long long>(pid));
        JsonRectField(ss, depth + 1, L"bounds", r);
    } else if (nd->kind == NodeData::Kind::Element) {
        ExportElementJsonFields(nd->disappeared ? nullptr : nd->element, depth + 1, ss, displayText);
        JsonBoolField(ss, depth + 1, L"disappeared", nd->disappeared);
    }

    EnsureItemLoaded(item);

    JsonIndent(ss, depth + 1);
    ss << L"\"children\": [";

    bool hasChild = false;
    HTREEITEM child = TreeView_GetChild(g_hTree, item);
    while (child && count < 10000) {
        HTREEITEM next = TreeView_GetNextSibling(g_hTree, child);
        if (IsExportableTreeItem(child)) {
            if (!hasChild) {
                ss << L"\r\n";
            } else {
                ss << L",\r\n";
            }
            ExportTreeItemJsonRecursive(child, depth + 2, ss, count);
            hasChild = true;
        }
        child = next;
    }

    if (hasChild) {
        ss << L"\r\n";
        JsonIndent(ss, depth + 1);
    }
    ss << L"]\r\n";

    JsonIndent(ss, depth);
    ss << L"}";
    return true;
}

static std::wstring BuildExportJson(const std::vector<HTREEITEM>& roots, const wchar_t* exportType, int& count) {
    std::wstringstream ss;
    count = 0;

    ss << L"{\r\n";
    JsonStringField(ss, 1, L"tool", L"杰睿元素树分析工具");
    JsonStringField(ss, 1, L"format", L"JeriUiaExplorerJson");
    JsonStringField(ss, 1, L"exportType", exportType);
    JsonNumberField(ss, 1, L"maxNodeCount", 10000);
    JsonIndent(ss, 1);
    ss << L"\"items\": [";

    bool hasItem = false;
    for (HTREEITEM item : roots) {
        if (!IsExportableTreeItem(item) || count >= 10000) continue;
        if (!hasItem) {
            ss << L"\r\n";
        } else {
            ss << L",\r\n";
        }
        ExportTreeItemJsonRecursive(item, 2, ss, count);
        hasItem = true;
    }

    if (hasItem) {
        ss << L"\r\n";
        JsonIndent(ss, 1);
    }
    ss << L"],\r\n";
    JsonNumberField(ss, 1, L"count", count, false);
    ss << L"}\r\n";
    return ss.str();
}

static bool WriteUtf8TextFile(const wchar_t* path, const std::wstring& text) {
    HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;

    if (text.empty()) {
        CloseHandle(h);
        return true;
    }

    int len = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (len <= 0) {
        CloseHandle(h);
        return false;
    }

    std::string utf8(static_cast<size_t>(len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), &utf8[0], len, nullptr, nullptr);

    DWORD written = 0;
    BOOL ok = WriteFile(h, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
    CloseHandle(h);
    return ok == TRUE && written == utf8.size();
}

static void ExportSingleTreeItem(HTREEITEM item) {
    NodeData* nd = GetNodeData(item);
    if (!item || !nd || nd->kind == NodeData::Kind::Placeholder) {
        MessageBoxW(g_hMain, L"请在左侧树中右键需要导出的有效项目。", L"导出提示", MB_OK | MB_ICONINFORMATION);
        SetStatusText( L"没有可导出的右键项目。");
        return;
    }

    wchar_t fileName[MAX_PATH] = L"JeriUiaExplorer_item_export.json";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_hMain;
    ofn.lpstrFilter = L"JSON文件 (*.json)\0*.json\0所有文件 (*.*)\0*.*\0";
    ofn.lpstrFile = fileName;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = L"json";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    ofn.lpstrTitle = L"选择导出位置";

    if (!GetSaveFileNameW(&ofn)) {
        SetStatusText( L"已取消导出。");
        return;
    }

    SetStatusText( L"正在以 JSON 导出右键项及其子项，请稍候...");
    std::vector<HTREEITEM> roots;
    roots.push_back(item);

    int count = 0;
    std::wstring json = BuildExportJson(roots, L"singleItem", count);

    if (!WriteUtf8TextFile(fileName, json)) {
        MessageBoxW(g_hMain, L"导出失败，请确认目标位置可写。", L"导出失败", MB_OK | MB_ICONERROR);
        SetStatusText( L"导出失败。");
        return;
    }

    std::wstringstream status;
    status << L"右键项导出完成，项目数量：" << count;
    SetStatusText( status.str().c_str());
}

static void ExportSelectedItems() {
    std::vector<HTREEITEM> roots;
    for (HTREEITEM item : g_selectedItems) {
        NodeData* nd = GetNodeData(item);
        if (item && nd && nd->kind != NodeData::Kind::Placeholder && !HasSelectedAncestor(item)) {
            roots.push_back(item);
        }
    }

    if (roots.empty()) {
        MessageBoxW(g_hMain, L"请先在左侧树中选中需要导出的项目。可按住 Ctrl 多选。", L"导出提示", MB_OK | MB_ICONINFORMATION);
        SetStatusText( L"没有选中项目，无法导出。");
        return;
    }

    wchar_t fileName[MAX_PATH] = L"JeriUiaExplorer_export.json";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_hMain;
    ofn.lpstrFilter = L"JSON文件 (*.json)\0*.json\0所有文件 (*.*)\0*.*\0";
    ofn.lpstrFile = fileName;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = L"json";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    ofn.lpstrTitle = L"选择导出位置";

    if (!GetSaveFileNameW(&ofn)) {
        SetStatusText( L"已取消导出。");
        return;
    }

    SetStatusText( L"正在以 JSON 导出选中项及其子项，请稍候...");

    int count = 0;
    std::wstring json = BuildExportJson(roots, L"selectedItems", count);

    if (!WriteUtf8TextFile(fileName, json)) {
        MessageBoxW(g_hMain, L"导出失败，请确认目标位置可写。", L"导出失败", MB_OK | MB_ICONERROR);
        SetStatusText( L"导出失败。");
        return;
    }

    std::wstringstream status;
    status << L"导出完成，项目数量：" << count;
    SetStatusText( status.str().c_str());
}

static bool SupportsPattern(IUIAutomationElement* e, PATTERNID pid, REFIID iid, const wchar_t* name, std::wstringstream& ss) {
    IUnknown* p = nullptr;
    HRESULT hr = e->GetCurrentPatternAs(pid, iid, reinterpret_cast<void**>(&p));
    if (SUCCEEDED(hr) && p) {
        ss << L"  · " << name << L"\r\n";
        p->Release();
        return true;
    }
    return false;
}

static IUIAutomationElement* GetNodeElementForAction(NodeData* nd) {
    if (!nd || nd->disappeared) return nullptr;
    if (nd->kind == NodeData::Kind::Element && nd->element) {
        nd->element->AddRef();
        return nd->element;
    }
    if (nd->kind == NodeData::Kind::Window && nd->hwnd && g_uia) {
        IUIAutomationElement* e = nullptr;
        HRESULT hr = g_uia->ElementFromHandle(nd->hwnd, &e);
        if (SUCCEEDED(hr) && e) return e;
    }
    return nullptr;
}

static bool ElementSupportsPattern(IUIAutomationElement* e, PATTERNID pid, REFIID iid) {
    if (!e) return false;
    IUnknown* p = nullptr;
    HRESULT hr = e->GetCurrentPatternAs(pid, iid, reinterpret_cast<void**>(&p));
    if (SUCCEEDED(hr) && p) {
        p->Release();
        return true;
    }
    return false;
}

static bool NodeSupportsPattern(NodeData* nd, PATTERNID pid, REFIID iid) {
    IUIAutomationElement* e = GetNodeElementForAction(nd);
    if (!e) return false;
    bool ok = ElementSupportsPattern(e, pid, iid);
    e->Release();
    return ok;
}

static void SetActionStatus(const wchar_t* actionName, HRESULT hr) {
    if (!g_hStatus) return;
    std::wstringstream ss;
    ss << actionName;
    if (SUCCEEDED(hr)) {
        ss << L"已完成。";
    } else {
        ss << L"失败，错误码：0x" << std::hex << static_cast<unsigned long>(hr) << std::dec;
    }
    SetStatusText( ss.str().c_str());
}

static HRESULT ExecuteInvokePattern(IUIAutomationElement* e) {
    IUIAutomationInvokePattern* p = nullptr;
    HRESULT hr = e->GetCurrentPatternAs(UIA_InvokePatternId, IID_IUIAutomationInvokePattern, reinterpret_cast<void**>(&p));
    if (SUCCEEDED(hr) && p) {
        hr = p->Invoke();
        p->Release();
    }
    return hr;
}

static HRESULT ExecuteTogglePattern(IUIAutomationElement* e) {
    IUIAutomationTogglePattern* p = nullptr;
    HRESULT hr = e->GetCurrentPatternAs(UIA_TogglePatternId, IID_IUIAutomationTogglePattern, reinterpret_cast<void**>(&p));
    if (SUCCEEDED(hr) && p) {
        hr = p->Toggle();
        p->Release();
    }
    return hr;
}

static HRESULT ExecuteSelectionItemPattern(IUIAutomationElement* e) {
    IUIAutomationSelectionItemPattern* p = nullptr;
    HRESULT hr = e->GetCurrentPatternAs(UIA_SelectionItemPatternId, IID_IUIAutomationSelectionItemPattern, reinterpret_cast<void**>(&p));
    if (SUCCEEDED(hr) && p) {
        hr = p->Select();
        p->Release();
    }
    return hr;
}

static HRESULT ExecuteExpandCollapsePattern(IUIAutomationElement* e, bool expand) {
    IUIAutomationExpandCollapsePattern* p = nullptr;
    HRESULT hr = e->GetCurrentPatternAs(UIA_ExpandCollapsePatternId, IID_IUIAutomationExpandCollapsePattern, reinterpret_cast<void**>(&p));
    if (SUCCEEDED(hr) && p) {
        hr = expand ? p->Expand() : p->Collapse();
        p->Release();
    }
    return hr;
}

static HRESULT ExecuteScrollItemPattern(IUIAutomationElement* e) {
    IUIAutomationScrollItemPattern* p = nullptr;
    HRESULT hr = e->GetCurrentPatternAs(UIA_ScrollItemPatternId, IID_IUIAutomationScrollItemPattern, reinterpret_cast<void**>(&p));
    if (SUCCEEDED(hr) && p) {
        hr = p->ScrollIntoView();
        p->Release();
    }
    return hr;
}

static HRESULT ExecuteWindowPattern(IUIAutomationElement* e, UINT cmd) {
    IUIAutomationWindowPattern* p = nullptr;
    HRESULT hr = e->GetCurrentPatternAs(UIA_WindowPatternId, IID_IUIAutomationWindowPattern, reinterpret_cast<void**>(&p));
    if (SUCCEEDED(hr) && p) {
        if (cmd == IDM_UIA_WINDOW_CLOSE) {
            hr = p->Close();
        } else if (cmd == IDM_UIA_WINDOW_MINIMIZE) {
            hr = p->SetWindowVisualState(WindowVisualState_Minimized);
        } else if (cmd == IDM_UIA_WINDOW_MAXIMIZE) {
            hr = p->SetWindowVisualState(WindowVisualState_Maximized);
        } else if (cmd == IDM_UIA_WINDOW_RESTORE) {
            hr = p->SetWindowVisualState(WindowVisualState_Normal);
        } else {
            hr = E_INVALIDARG;
        }
        p->Release();
    }
    return hr;
}

static const wchar_t* UiaActionName(UINT cmd) {
    switch (cmd) {
    case IDM_UIA_INVOKE: return L"UIA执行默认操作";
    case IDM_UIA_TOGGLE: return L"UIA切换状态";
    case IDM_UIA_SELECT: return L"UIA选择该项";
    case IDM_UIA_EXPAND: return L"UIA展开";
    case IDM_UIA_COLLAPSE: return L"UIA折叠";
    case IDM_UIA_SCROLL_INTO_VIEW: return L"UIA滚动到可见";
    case IDM_UIA_SET_FOCUS: return L"UIA设置焦点";
    case IDM_UIA_WINDOW_CLOSE: return L"UIA关闭窗口";
    case IDM_UIA_WINDOW_MINIMIZE: return L"UIA最小化窗口";
    case IDM_UIA_WINDOW_MAXIMIZE: return L"UIA最大化窗口";
    case IDM_UIA_WINDOW_RESTORE: return L"UIA还原窗口";
    default: return L"UIA操作";
    }
}

static void ExecuteUiaAction(HTREEITEM item, UINT cmd) {
    NodeData* nd = GetNodeData(item);
    if (!nd || nd->kind == NodeData::Kind::Placeholder) {
        SetStatusText( L"当前项目不能执行 UIA 操作。");
        return;
    }

    if (cmd == IDM_UIA_WINDOW_CLOSE) {
        int ret = MessageBoxW(g_hMain, L"确定要关闭该窗口吗？", L"关闭窗口确认", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2);
        if (ret != IDYES) {
            SetStatusText( L"已取消关闭窗口。");
            return;
        }
    }

    IUIAutomationElement* e = GetNodeElementForAction(nd);
    if (!e) {
        SetStatusText( L"无法获取该项目的 UIA 元素。");
        return;
    }

    HRESULT hr = E_FAIL;
    if (cmd == IDM_UIA_INVOKE) {
        hr = ExecuteInvokePattern(e);
    } else if (cmd == IDM_UIA_TOGGLE) {
        hr = ExecuteTogglePattern(e);
    } else if (cmd == IDM_UIA_SELECT) {
        hr = ExecuteSelectionItemPattern(e);
    } else if (cmd == IDM_UIA_EXPAND) {
        hr = ExecuteExpandCollapsePattern(e, true);
    } else if (cmd == IDM_UIA_COLLAPSE) {
        hr = ExecuteExpandCollapsePattern(e, false);
    } else if (cmd == IDM_UIA_SCROLL_INTO_VIEW) {
        hr = ExecuteScrollItemPattern(e);
    } else if (cmd == IDM_UIA_SET_FOCUS) {
        hr = e->SetFocus();
    } else if (cmd >= IDM_UIA_WINDOW_CLOSE && cmd <= IDM_UIA_WINDOW_RESTORE) {
        hr = ExecuteWindowPattern(e, cmd);
    } else {
        hr = E_INVALIDARG;
    }

    e->Release();

    if (SUCCEEDED(hr)) {
        Sleep(120);
        RefreshWindows();
    }
    SetActionStatus(UiaActionName(cmd), hr);
}

static bool SendMouseInput(DWORD flags) {
    INPUT input{};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = flags;
    return SendInput(1, &input, sizeof(INPUT)) == 1;
}

static POINT CenterPointFromRect(const RECT& r) {
    POINT pt{};
    pt.x = r.left + (r.right - r.left) / 2;
    pt.y = r.top + (r.bottom - r.top) / 2;
    return pt;
}

static bool MoveCursorToPoint(const POINT& pt) {
    return SetCursorPos(pt.x, pt.y) != FALSE;
}

static bool SendLeftClickAt(const POINT& pt, int count) {
    if (!MoveCursorToPoint(pt)) return false;
    Sleep(60);
    int delay = static_cast<int>(GetDoubleClickTime() / 3);
    if (delay < 60) delay = 60;
    if (delay > 180) delay = 180;
    for (int i = 0; i < count; ++i) {
        if (!SendMouseInput(MOUSEEVENTF_LEFTDOWN)) return false;
        Sleep(40);
        if (!SendMouseInput(MOUSEEVENTF_LEFTUP)) return false;
        if (i + 1 < count) Sleep(delay);
    }
    return true;
}

static bool SendRightClickAt(const POINT& pt) {
    if (!MoveCursorToPoint(pt)) return false;
    Sleep(60);
    if (!SendMouseInput(MOUSEEVENTF_RIGHTDOWN)) return false;
    Sleep(40);
    if (!SendMouseInput(MOUSEEVENTF_RIGHTUP)) return false;
    return true;
}

static bool SendDrag(const POINT& from, const POINT& to) {
    if (!MoveCursorToPoint(from)) return false;
    Sleep(80);
    if (!SendMouseInput(MOUSEEVENTF_LEFTDOWN)) return false;
    Sleep(80);
    const int steps = 24;
    for (int i = 1; i <= steps; ++i) {
        POINT pt{};
        pt.x = from.x + (to.x - from.x) * i / steps;
        pt.y = from.y + (to.y - from.y) * i / steps;
        if (!MoveCursorToPoint(pt)) {
            SendMouseInput(MOUSEEVENTF_LEFTUP);
            return false;
        }
        Sleep(10);
    }
    Sleep(60);
    if (!SendMouseInput(MOUSEEVENTF_LEFTUP)) return false;
    return true;
}

static void SetMouseActionStatus(const wchar_t* actionName, bool ok) {
    if (!g_hStatus) return;
    std::wstringstream ss;
    ss << actionName << (ok ? L"已完成。" : L"失败，可能被权限隔离或目标窗口状态阻止。");
    SetStatusText( ss.str().c_str());
}

static const wchar_t* MouseActionName(UINT cmd) {
    switch (cmd) {
    case IDM_MOUSE_MOVE_CENTER: return L"移动鼠标到元素中心";
    case IDM_MOUSE_LEFT_CLICK: return L"左键单击";
    case IDM_MOUSE_LEFT_DOUBLE_CLICK: return L"左键双击";
    case IDM_MOUSE_RIGHT_CLICK: return L"右键单击";
    case IDM_MOUSE_DRAG_TO_CURSOR: return L"拖动";
    default: return L"鼠标模拟";
    }
}

static void ExecuteMouseAction(HTREEITEM item, UINT cmd) {
    NodeData* nd = GetNodeData(item);
    if (!nd || nd->kind == NodeData::Kind::Placeholder) {
        SetStatusText( L"当前项目不能执行鼠标模拟。");
        return;
    }

    RECT r{};
    if (!GetNodeBounds(nd, r)) {
        SetStatusText( L"当前项目没有可用屏幕区域，无法执行鼠标模拟。");
        return;
    }

    POINT center = CenterPointFromRect(r);
    bool ok = false;

    if (cmd == IDM_MOUSE_MOVE_CENTER) {
        ok = MoveCursorToPoint(center);
    } else if (cmd == IDM_MOUSE_LEFT_CLICK) {
        ok = SendLeftClickAt(center, 1);
    } else if (cmd == IDM_MOUSE_LEFT_DOUBLE_CLICK) {
        ok = SendLeftClickAt(center, 2);
    } else if (cmd == IDM_MOUSE_RIGHT_CLICK) {
        ok = SendRightClickAt(center);
    } else if (cmd == IDM_MOUSE_DRAG_TO_CURSOR) {
        int ret = MessageBoxW(g_hMain,
            L"点击确定后，请在 2 秒内把鼠标移动到拖动终点。\r\n工具会从当前元素中心按住左键并拖动到该位置。",
            L"SendInput拖动", MB_OKCANCEL | MB_ICONINFORMATION);
        if (ret != IDOK) {
            SetStatusText( L"已取消拖动。");
            return;
        }
        SetStatusText( L"请移动鼠标到拖动终点...");
        Sleep(2000);
        POINT target{};
        GetCursorPos(&target);
        ok = SendDrag(center, target);
    }

    if (ok) {
        Sleep(120);
        RefreshWindows();
    }
    SetMouseActionStatus(MouseActionName(cmd), ok);
}

static void ShowWindowDetail(NodeData* nd) {
    if (!nd) return;
    std::wstringstream ss;
    ss << L"窗口\r\n";
    ss << L"状态：" << (nd->disappeared ? L"已消失（保留记录）" : L"当前存在") << L"\r\n";
    ss << L"标题：" << nd->title << L"\r\n";
    ss << L"窗口句柄：" << std::hex << reinterpret_cast<UINT_PTR>(nd->hwnd) << std::dec << L"\r\n";

    if (!nd->disappeared && nd->hwnd && IsWindow(nd->hwnd)) {
        wchar_t cls[256]{};
        GetClassNameW(nd->hwnd, cls, 256);
        ss << L"窗口类名：" << cls << L"\r\n";

        DWORD pid = 0;
        GetWindowThreadProcessId(nd->hwnd, &pid);
        ss << L"进程编号：" << pid << L"\r\n";

        RECT r{};
        GetWindowRect(nd->hwnd, &r);
        ss << L"区域：" << RectToString(r) << L"\r\n";
    } else {
        ss << L"说明：窗口已不在当前可见窗口列表中，保留用于观察刷新变化。\r\n";
    }
    SetWindowTextW(g_hDetail, ss.str().c_str());
}

static void ShowElementDetail(IUIAutomationElement* e) {
    if (!e) return;
    std::wstringstream ss;
    BSTR name = nullptr, aid = nullptr, cls = nullptr, fw = nullptr;
    CONTROLTYPEID ct = 0;
    RECT rect{};
    BOOL enabled = FALSE, offscreen = FALSE, focusable = FALSE, focused = FALSE;
    int pid = 0;

    e->get_CurrentName(&name);
    e->get_CurrentAutomationId(&aid);
    e->get_CurrentClassName(&cls);
    e->get_CurrentFrameworkId(&fw);
    e->get_CurrentControlType(&ct);
    e->get_CurrentBoundingRectangle(&rect);
    e->get_CurrentIsEnabled(&enabled);
    e->get_CurrentIsOffscreen(&offscreen);
    e->get_CurrentIsKeyboardFocusable(&focusable);
    e->get_CurrentHasKeyboardFocus(&focused);
    e->get_CurrentProcessId(&pid);

    ss << L"界面自动化元素\r\n";
    ss << L"名称：" << BstrToWString(name) << L"\r\n";
    ss << L"控件类型：" << ControlTypeName(ct) << L"（" << ct << L"）\r\n";
    ss << L"自动化标识：" << BstrToWString(aid) << L"\r\n";
    ss << L"窗口类名：" << BstrToWString(cls) << L"\r\n";
    ss << L"框架标识：" << BstrToWString(fw) << L"\r\n";
    ss << L"进程编号：" << pid << L"\r\n";
    ss << L"区域：" << RectToString(rect) << L"\r\n";
    ss << L"是否启用：" << (enabled ? L"是" : L"否") << L"\r\n";
    ss << L"是否在屏幕外：" << (offscreen ? L"是" : L"否") << L"\r\n";
    ss << L"是否可键盘聚焦：" << (focusable ? L"是" : L"否") << L"\r\n";
    ss << L"是否已有键盘焦点：" << (focused ? L"是" : L"否") << L"\r\n";
    ss << L"\r\n支持的操作模式：\r\n";

    bool any = false;
    any |= SupportsPattern(e, UIA_InvokePatternId, IID_IUIAutomationInvokePattern, L"调用模式：可点击或可调用", ss);
    any |= SupportsPattern(e, UIA_ValuePatternId, IID_IUIAutomationValuePattern, L"取值模式：可读取或输入值", ss);
    any |= SupportsPattern(e, UIA_TextPatternId, IID_IUIAutomationTextPattern, L"文本模式：提供文本内容", ss);
    any |= SupportsPattern(e, UIA_TogglePatternId, IID_IUIAutomationTogglePattern, L"切换模式：复选框或开关", ss);
    any |= SupportsPattern(e, UIA_SelectionItemPatternId, IID_IUIAutomationSelectionItemPattern, L"选择项模式：可选择项目", ss);
    any |= SupportsPattern(e, UIA_ExpandCollapsePatternId, IID_IUIAutomationExpandCollapsePattern, L"展开折叠模式：可展开或收起", ss);
    any |= SupportsPattern(e, UIA_RangeValuePatternId, IID_IUIAutomationRangeValuePattern, L"范围取值模式：滑块或范围值", ss);
    any |= SupportsPattern(e, UIA_ScrollPatternId, IID_IUIAutomationScrollPattern, L"滚动模式：可滚动区域", ss);
    any |= SupportsPattern(e, UIA_ScrollItemPatternId, IID_IUIAutomationScrollItemPattern, L"滚动项模式：可滚动到该项目", ss);
    any |= SupportsPattern(e, UIA_WindowPatternId, IID_IUIAutomationWindowPattern, L"窗口模式：窗口控制能力", ss);
    any |= SupportsPattern(e, UIA_DockPatternId, IID_IUIAutomationDockPattern, L"停靠模式", ss);
    any |= SupportsPattern(e, UIA_GridPatternId, IID_IUIAutomationGridPattern, L"网格模式", ss);
    any |= SupportsPattern(e, UIA_GridItemPatternId, IID_IUIAutomationGridItemPattern, L"网格项模式", ss);
    any |= SupportsPattern(e, UIA_TablePatternId, IID_IUIAutomationTablePattern, L"表格模式", ss);
    any |= SupportsPattern(e, UIA_TableItemPatternId, IID_IUIAutomationTableItemPattern, L"表格项模式", ss);
    if (!any) ss << L"  （无）\r\n";

    if (name) SysFreeString(name);
    if (aid) SysFreeString(aid);
    if (cls) SysFreeString(cls);
    if (fw) SysFreeString(fw);

    SetWindowTextW(g_hDetail, ss.str().c_str());
}

static void SetTreeItemTextOnly(HTREEITEM item, const std::wstring& text) {
    if (!item) return;
    TVITEMW tv{};
    tv.mask = TVIF_TEXT;
    tv.hItem = item;
    tv.pszText = const_cast<LPWSTR>(text.c_str());
    TreeView_SetItem(g_hTree, &tv);
}

static void UpdateTreeItemText(HTREEITEM item, const std::wstring& text) {
    NodeData* nd = GetNodeData(item);
    if (!nd) return;
    if (nd->displayText != text) {
        nd->displayText = text;
        SetTreeItemTextOnly(item, text);
        MarkTreeItemChanged(item);
    }
}

static void SetTreeItemDisappeared(HTREEITEM item, bool disappeared) {
    NodeData* nd = GetNodeData(item);
    if (!nd || nd->kind == NodeData::Kind::Placeholder) return;
    if (nd->disappeared != disappeared) {
        nd->disappeared = disappeared;
        InvalidateTreeItem(item);
        if (disappeared) MarkTreeItemChanged(TreeView_GetParent(g_hTree, item));
        else MarkTreeItemChanged(item);
    }
}

static void SetSubtreeDisappeared(HTREEITEM item, bool disappeared) {
    if (!item) return;
    SetTreeItemDisappeared(item, disappeared);
    HTREEITEM child = TreeView_GetChild(g_hTree, item);
    while (child) {
        HTREEITEM next = TreeView_GetNextSibling(g_hTree, child);
        SetSubtreeDisappeared(child, disappeared);
        child = next;
    }
}

static void ClearChangedMarks() {
    if (g_changedItems.empty()) return;

    std::vector<HTREEITEM> items;
    items.swap(g_changedItems);
    for (HTREEITEM item : items) {
        if (!IsTreeItemAlive(item)) continue;
        NodeData* nd = GetNodeData(item);
        if (nd && nd->changed) {
            nd->changed = false;
            InvalidateTreeItem(item);
        }
    }
}

struct WindowSnapshot {
    HWND hwnd = nullptr;
    std::wstring title;
    std::wstring className;
    std::wstring displayText;
    bool shellSurface = false;
};

struct ElementSnapshot {
    IUIAutomationElement* element = nullptr;
    std::wstring runtimeId;
    std::wstring displayText;
    bool hasChildren = false;
};

static std::wstring BuildWindowDisplayText(const std::wstring& title, const std::wstring& className) {
    std::wstring safeTitle = title.empty() ? L"（无标题）" : title;
    return L"[窗口] " + safeTitle + L"  （类名：" + className + L"）";
}

static std::wstring BuildShellDisplayText(const std::wstring& title, const std::wstring& className) {
    return L"[系统] " + title + L"  （类名：" + className + L"）";
}

static std::wstring GetWindowClassNameString(HWND hwnd) {
    wchar_t cls[256]{};
    GetClassNameW(hwnd, cls, 256);
    return cls;
}

static std::wstring GetWindowTitleString(HWND hwnd) {
    wchar_t title[512]{};
    GetWindowTextW(hwnd, title, 512);
    return title;
}

static bool IsShellSurfaceClass(const std::wstring& className) {
    return className == L"Progman" ||
           className == L"WorkerW" ||
           className == L"SHELLDLL_DefView" ||
           className == L"SysListView32" ||
           className == L"Shell_TrayWnd" ||
           className == L"Shell_SecondaryTrayWnd";
}

static HWND FindDesktopDefViewWindow() {
    HWND progman = FindWindowW(L"Progman", nullptr);
    HWND defView = progman ? FindWindowExW(progman, nullptr, L"SHELLDLL_DefView", nullptr) : nullptr;
    if (defView) return defView;

    HWND worker = nullptr;
    while ((worker = FindWindowExW(nullptr, worker, L"WorkerW", nullptr)) != nullptr) {
        defView = FindWindowExW(worker, nullptr, L"SHELLDLL_DefView", nullptr);
        if (defView) return defView;
    }
    return progman;
}

static HWND FindDesktopIconHostWindow() {
    HWND defView = FindDesktopDefViewWindow();
    if (!defView) return nullptr;
    HWND listView = FindWindowExW(defView, nullptr, L"SysListView32", nullptr);
    return listView ? listView : defView;
}

static std::wstring GetShellSurfaceTitle(HWND hwnd, const std::wstring& className) {
    if (className == L"Shell_TrayWnd") return L"任务栏";
    if (className == L"Shell_SecondaryTrayWnd") return L"辅屏任务栏";

    if (className == L"SysListView32" || className == L"SHELLDLL_DefView" ||
        className == L"Progman" || className == L"WorkerW") {
        HWND iconHost = FindDesktopIconHostWindow();
        HWND defView = FindDesktopDefViewWindow();
        if (hwnd == iconHost) return L"桌面程序/图标";
        if (hwnd == defView) return L"桌面";
    }
    return L"";
}

static bool AppendWindowSnapshotIfValid(std::vector<WindowSnapshot>& list, HWND hwnd, bool allowShellSurface) {
    if (!hwnd || hwnd == g_hMain) return false;
    if (!IsWindow(hwnd) || !IsWindowVisible(hwnd)) return false;

    RECT r{};
    GetWindowRect(hwnd, &r);
    if ((r.right - r.left) <= 0 || (r.bottom - r.top) <= 0) return false;

    std::wstring className = GetWindowClassNameString(hwnd);
    std::wstring shellTitle = allowShellSurface ? GetShellSurfaceTitle(hwnd, className) : L"";
    if (shellTitle.empty()) {
        if (GetWindow(hwnd, GW_OWNER)) return false;
        if (IsShellSurfaceClass(className)) return false;
    }

    std::wstring title = shellTitle.empty() ? GetWindowTitleString(hwnd) : shellTitle;
    if (title.empty()) return false;

    for (const WindowSnapshot& existing : list) {
        if (existing.hwnd == hwnd) return false;
    }

    WindowSnapshot snap;
    snap.hwnd = hwnd;
    snap.title = title;
    snap.className = className;
    snap.shellSurface = !shellTitle.empty();
    snap.displayText = snap.shellSurface ? BuildShellDisplayText(snap.title, snap.className)
                                         : BuildWindowDisplayText(snap.title, snap.className);
    list.push_back(snap);
    return true;
}

static void CollectShellSurfaceSnapshots(std::vector<WindowSnapshot>& list) {
    AppendWindowSnapshotIfValid(list, FindDesktopIconHostWindow(), true);
    AppendWindowSnapshotIfValid(list, FindWindowW(L"Shell_TrayWnd", nullptr), true);

    HWND secondary = nullptr;
    while ((secondary = FindWindowExW(nullptr, secondary, L"Shell_SecondaryTrayWnd", nullptr)) != nullptr) {
        AppendWindowSnapshotIfValid(list, secondary, true);
    }
}

static BOOL CALLBACK CollectWindowSnapshotProc(HWND hwnd, LPARAM lParam) {
    auto* list = reinterpret_cast<std::vector<WindowSnapshot>*>(lParam);
    if (list) AppendWindowSnapshotIfValid(*list, hwnd, false);
    return TRUE;
}

static bool IsValidWindowForTree(HWND hwnd) {
    std::vector<WindowSnapshot> snaps;
    return AppendWindowSnapshotIfValid(snaps, hwnd, true);
}

static bool UpdateWindowItemInPlace(HTREEITEM item) {
    NodeData* nd = GetNodeData(item);
    if (!nd || nd->kind != NodeData::Kind::Window) return false;

    std::vector<WindowSnapshot> snaps;
    if (!AppendWindowSnapshotIfValid(snaps, nd->hwnd, true) || snaps.empty()) {
        SetSubtreeDisappeared(item, true);
        return false;
    }

    SetTreeItemDisappeared(item, false);
    nd->title = snaps[0].title;
    UpdateTreeItemText(item, snaps[0].displayText);
    return true;
}

static HTREEITEM NormalizeRefreshScopeItem(HTREEITEM item) {
    if (!item || !IsTreeItemAlive(item)) return nullptr;
    NodeData* nd = GetNodeData(item);
    if (nd && nd->kind == NodeData::Kind::Placeholder) {
        item = TreeView_GetParent(g_hTree, item);
    }
    return item && IsTreeItemAlive(item) ? item : nullptr;
}

static void AddRefreshScopeItem(std::vector<HTREEITEM>& list, HTREEITEM item) {
    item = NormalizeRefreshScopeItem(item);
    if (!item) return;

    for (HTREEITEM existing : list) {
        if (existing == item || IsTreeItemSameOrChildOf(item, existing)) return;
    }

    list.erase(
        std::remove_if(list.begin(), list.end(),
            [item](HTREEITEM existing) { return IsTreeItemSameOrChildOf(existing, item); }),
        list.end());
    list.push_back(item);
}

static std::vector<HTREEITEM> GetRefreshScopeItems() {
    std::vector<HTREEITEM> items;
    for (HTREEITEM selected : g_selectedItems) {
        AddRefreshScopeItem(items, selected);
    }

    HTREEITEM current = TreeView_GetSelection(g_hTree);
    AddRefreshScopeItem(items, current);
    return items;
}

static bool IsItemInList(const std::vector<HTREEITEM>& items, HTREEITEM item) {
    return std::find(items.begin(), items.end(), item) != items.end();
}

static HTREEITEM FindElementChildByRuntimeId(HTREEITEM parent, const std::wstring& runtimeId, const std::vector<HTREEITEM>& used) {
    if (runtimeId.empty()) return nullptr;
    HTREEITEM child = TreeView_GetChild(g_hTree, parent);
    while (child) {
        if (!IsItemInList(used, child)) {
            NodeData* nd = GetNodeData(child);
            if (nd && nd->kind == NodeData::Kind::Element && nd->runtimeId == runtimeId) return child;
        }
        child = TreeView_GetNextSibling(g_hTree, child);
    }
    return nullptr;
}

static HTREEITEM FindElementChildByPosition(HTREEITEM parent, int elementIndex, const std::vector<HTREEITEM>& used) {
    int index = 0;
    HTREEITEM child = TreeView_GetChild(g_hTree, parent);
    while (child) {
        if (!IsItemInList(used, child)) {
            NodeData* nd = GetNodeData(child);
            if (nd && nd->kind == NodeData::Kind::Element && !nd->disappeared) {
                if (index == elementIndex) return child;
                ++index;
            }
        }
        child = TreeView_GetNextSibling(g_hTree, child);
    }
    return nullptr;
}

static void ReleaseElementSnapshots(std::vector<ElementSnapshot>& list) {
    for (ElementSnapshot& snap : list) {
        if (snap.element) {
            snap.element->Release();
            snap.element = nullptr;
        }
    }
    list.clear();
}

static void CollectElementSnapshots(IUIAutomationElement* parentElem, std::vector<ElementSnapshot>& list) {
    if (!g_uia || !parentElem) return;
    IUIAutomationTreeWalker* walker = GetControlViewWalker();
    if (!walker) return;

    IUIAutomationElement* child = nullptr;
    HRESULT hr = walker->GetFirstChildElement(parentElem, &child);
    while (SUCCEEDED(hr) && child) {
        ElementSnapshot snap;
        snap.element = child;
        snap.runtimeId = ElementRuntimeId(child);
        snap.displayText = ElementText(child);
        // 刷新时大多数子节点已存在，是否存在子节点只在新增节点时再按需查询。
        snap.hasChildren = false;
        list.push_back(snap);

        IUIAutomationElement* next = nullptr;
        hr = walker->GetNextSiblingElement(child, &next);
        child = next;
    }
}

static void RefreshElementChildrenInPlace(HTREEITEM parentItem, IUIAutomationElement* parentElem) {
    if (!parentItem || !parentElem) return;

    std::vector<ElementSnapshot> snaps;
    CollectElementSnapshots(parentElem, snaps);

    if (snaps.empty()) {
        HTREEITEM child = TreeView_GetChild(g_hTree, parentItem);
        bool hasRealChild = false;
        bool changed = false;
        while (child) {
            NodeData* cd = GetNodeData(child);
            if (cd && cd->kind != NodeData::Kind::Placeholder) {
                hasRealChild = true;
                if (!cd->disappeared) {
                    SetSubtreeDisappeared(child, true);
                    changed = true;
                }
            }
            child = TreeView_GetNextSibling(g_hTree, child);
        }
        if (!hasRealChild && !TreeView_GetChild(g_hTree, parentItem)) {
            NodeData* empty = MakeNode(NodeData::Kind::Placeholder);
            InsertTreeItem(parentItem, L"（无子元素）", empty, false, true);
            changed = true;
        }
        if (changed) MarkTreeItemChanged(parentItem);
        ReleaseElementSnapshots(snaps);
        return;
    }

    std::vector<HTREEITEM> used;
    for (size_t i = 0; i < snaps.size(); ++i) {
        ElementSnapshot& snap = snaps[i];
        HTREEITEM item = FindElementChildByRuntimeId(parentItem, snap.runtimeId, used);
        if (!item) item = FindElementChildByPosition(parentItem, static_cast<int>(i), used);

        if (item) {
            SetTreeItemDisappeared(item, false);
            NodeData* nd = GetNodeData(item);
            if (nd) {
                SetNodeElement(nd, snap.element);
                nd->runtimeId = snap.runtimeId;
                UpdateTreeItemText(item, snap.displayText);
            }
            used.push_back(item);
        } else {
            NodeData* nd = MakeNode(NodeData::Kind::Element);
            SetNodeElement(nd, snap.element);
            nd->runtimeId = snap.runtimeId;
            snap.hasChildren = ElementHasChildren(snap.element);
            HTREEITEM newItem = InsertTreeItem(parentItem, snap.displayText, nd, snap.hasChildren, true);
            used.push_back(newItem);
            MarkTreeItemChanged(parentItem);
        }
    }

    HTREEITEM child = TreeView_GetChild(g_hTree, parentItem);
    while (child) {
        HTREEITEM next = TreeView_GetNextSibling(g_hTree, child);
        if (!IsItemInList(used, child)) {
            NodeData* cd = GetNodeData(child);
            if (cd && cd->kind == NodeData::Kind::Placeholder) {
                DeleteTreeItemSafe(child);
                MarkTreeItemChanged(parentItem);
            } else if (cd && !cd->disappeared) {
                SetSubtreeDisappeared(child, true);
                MarkTreeItemChanged(parentItem);
            }
        }
        child = next;
    }

    ReleaseElementSnapshots(snaps);
}

static void RefreshLoadedItemInPlace(HTREEITEM item) {
    if (!item) return;
    NodeData* nd = GetNodeData(item);
    if (!nd || nd->kind == NodeData::Kind::Placeholder || nd->disappeared) return;

    if (nd->kind == NodeData::Kind::Window) {
        if (nd->loaded && g_uia && nd->hwnd && IsWindow(nd->hwnd)) {
            IUIAutomationElement* root = nullptr;
            HRESULT hr = g_uia->ElementFromHandle(nd->hwnd, &root);
            if (SUCCEEDED(hr) && root) {
                std::wstring runtimeId = ElementRuntimeId(root);
                std::wstring displayText = ElementText(root);
                HTREEITEM rootItem = TreeView_GetChild(g_hTree, item);
                NodeData* rootNode = GetNodeData(rootItem);
                bool sameRoot = rootNode && rootNode->kind == NodeData::Kind::Element &&
                                (runtimeId.empty() || rootNode->runtimeId.empty() || runtimeId == rootNode->runtimeId);
                if (sameRoot) {
                    SetTreeItemDisappeared(rootItem, false);
                    SetNodeElement(rootNode, root);
                    rootNode->runtimeId = runtimeId;
                    UpdateTreeItemText(rootItem, displayText);
                    if (rootNode->loaded) {
                        RefreshElementChildrenInPlace(rootItem, rootNode->element);
                        HTREEITEM child = TreeView_GetChild(g_hTree, rootItem);
                        while (child) {
                            HTREEITEM next = TreeView_GetNextSibling(g_hTree, child);
                            RefreshLoadedItemInPlace(child);
                            child = next;
                        }
                    }
                } else {
                    HTREEITEM oldChild = TreeView_GetChild(g_hTree, item);
                    if (oldChild) SetSubtreeDisappeared(oldChild, true);
                    NodeData* newRoot = MakeNode(NodeData::Kind::Element);
                    SetNodeElement(newRoot, root);
                    newRoot->runtimeId = runtimeId;
                    bool hasChildren = ElementHasChildren(root);
                    HTREEITEM newRootItem = InsertTreeItem(item, displayText, newRoot, hasChildren, true);
                    TreeView_Expand(g_hTree, newRootItem, TVE_EXPAND);
                    MarkTreeItemChanged(item);
                }
                root->Release();
            }
        }
        return;
    }

    if (nd->kind == NodeData::Kind::Element) {
        if (nd->element) {
            std::wstring runtimeId = ElementRuntimeId(nd->element);
            if (!runtimeId.empty()) nd->runtimeId = runtimeId;
            UpdateTreeItemText(item, ElementText(nd->element));
            if (nd->loaded) RefreshElementChildrenInPlace(item, nd->element);
        }
    }

    HTREEITEM child = TreeView_GetChild(g_hTree, item);
    while (child) {
        HTREEITEM next = TreeView_GetNextSibling(g_hTree, child);
        RefreshLoadedItemInPlace(child);
        child = next;
    }
}

static void SyncWindowNodesInPlace() {
    std::vector<WindowSnapshot> windows;
    CollectShellSurfaceSnapshots(windows);
    EnumWindows(CollectWindowSnapshotProc, reinterpret_cast<LPARAM>(&windows));

    std::unordered_map<HWND, const WindowSnapshot*> windowMap;
    windowMap.reserve(windows.size());
    for (const WindowSnapshot& snap : windows) {
        windowMap[snap.hwnd] = &snap;
    }

    std::unordered_map<HWND, HTREEITEM> existingItems;
    HTREEITEM item = TreeView_GetRoot(g_hTree);
    while (item) {
        HTREEITEM next = TreeView_GetNextSibling(g_hTree, item);
        NodeData* nd = GetNodeData(item);
        if (nd && nd->kind == NodeData::Kind::Window) {
            existingItems[nd->hwnd] = item;
            if (windowMap.find(nd->hwnd) == windowMap.end()) {
                SetSubtreeDisappeared(item, true);
            }
        }
        item = next;
    }

    for (const WindowSnapshot& snap : windows) {
        auto it = existingItems.find(snap.hwnd);
        if (it != existingItems.end()) {
            HTREEITEM existing = it->second;
            NodeData* nd = GetNodeData(existing);
            SetTreeItemDisappeared(existing, false);
            if (nd) nd->title = snap.title;
            UpdateTreeItemText(existing, snap.displayText);
        } else {
            NodeData* nd = MakeNode(NodeData::Kind::Window);
            nd->hwnd = snap.hwnd;
            nd->title = snap.title;
            nd->displayText = snap.displayText;
            InsertTreeItem(TVI_ROOT, snap.displayText, nd, true, true);
        }
    }
}

static std::wstring RefreshIntervalText(int scopeCount) {
    std::wstringstream ss;
    ss << L"自动刷新中，间隔" << g_autoRefreshIntervalMs << L"毫秒；窗口列表始终同步，消失项灰显保留。";
    if (scopeCount > 0) {
        ss << L" 正在刷新" << scopeCount << L"个选中范围及其已加载子节点。";
    } else {
        ss << L" 未选择节点，仅刷新窗口列表，不刷新元素内容。";
    }
    return ss.str();
}

static BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam) {
    return CollectWindowSnapshotProc(hwnd, lParam);
}

static void RefreshWindowRoots() {
    if (!g_hTree) return;
    {
        ScopedTreeRedraw treeRedraw;
        ClearChangedMarks();
        SyncWindowNodesInPlace();
    }
    RefreshAnnotationAfterTreeRefresh();
    SetStatusText( L"窗口列表已初始化。已支持桌面程序/图标、任务栏和普通窗口；选择节点后刷新其已加载子树。");
}

static void RefreshWindows() {
    if (!g_hTree) return;

    RefreshSearchGuard searchGuard;
    std::vector<HTREEITEM> scopes;
    {
        ScopedTreeRedraw treeRedraw;
        ClearChangedMarks();

        // 始终同步根窗口列表，避免新打开的窗口无法显示。
        // 这里仅增删/更新根窗口节点，不递归刷新未选中的元素树。
        SyncWindowNodesInPlace();

        // 根列表同步可能删除已失效窗口，所以刷新范围必须在同步后重新计算。
        scopes = GetRefreshScopeItems();

        for (HTREEITEM item : scopes) {
            if (!IsTreeItemAlive(item)) continue;
            NodeData* nd = GetNodeData(item);
            if (!nd || nd->kind == NodeData::Kind::Placeholder) continue;

            if (nd->kind == NodeData::Kind::Window) {
                if (!UpdateWindowItemInPlace(item)) continue;
            }
            if (IsTreeItemAlive(item)) RefreshLoadedItemInPlace(item);
        }
    }

    RefreshAnnotationAfterTreeRefresh();
    SetStatusText( RefreshIntervalText(static_cast<int>(scopes.size())).c_str());
}

static void ResetAutoRefreshTimer(HWND hwnd) {
    if (!hwnd || g_refreshPaused) return;
    KillTimer(hwnd, IDT_AUTO_REFRESH);
    SetTimer(hwnd, IDT_AUTO_REFRESH, g_autoRefreshIntervalMs, nullptr);
}

static void UpdateRefreshButtonText() {
    if (!g_hRefresh) return;
    SetWindowTextIfChanged(g_hRefresh, g_refreshPaused ? L"继续刷新" : L"暂停刷新");
}

static void SetAutoRefreshPaused(HWND hwnd, bool paused) {
    if (!paused && g_reverseLookupActive) {
        StopReverseLookup(L"继续刷新前已退出鼠标反找。");
    }
    g_refreshPaused = paused;
    ClearSearchResults();
    if (g_refreshPaused) {
        KillTimer(hwnd, IDT_AUTO_REFRESH);
        SetStatusText( L"已暂停自动刷新。可稳定展开、查看、搜索、反找和导出元素树。");
    } else {
        SetTimer(hwnd, IDT_AUTO_REFRESH, g_autoRefreshIntervalMs, nullptr);
        RefreshWindows();
    }
    UpdateRefreshButtonText();
    UpdateSearchControlsEnabled();
}

static void ApplyRefreshIntervalFromEdit(HWND hwnd) {
    if (!g_hIntervalEdit) return;
    wchar_t text[32]{};
    GetWindowTextW(g_hIntervalEdit, text, 32);

    wchar_t* end = nullptr;
    unsigned long value = wcstoul(text, &end, 10);
    if (value == 0 || value > 3600000UL) value = DEFAULT_AUTO_REFRESH_INTERVAL_MS;

    g_autoRefreshIntervalMs = static_cast<UINT>(value);
    std::wstringstream normalized;
    normalized << g_autoRefreshIntervalMs;
    SetWindowTextW(g_hIntervalEdit, normalized.str().c_str());

    ResetAutoRefreshTimer(hwnd);
    std::wstringstream ss;
    ss << L"刷新间隔已更新为" << g_autoRefreshIntervalMs << L"毫秒。";
    SetStatusText( ss.str().c_str());
}

static RECT GetSplitterRect(HWND hwnd) {
    RECT rc{};
    GetClientRect(hwnd, &rc);
    const int margin = 8;
    const int top = 8;
    const int btnH = 30;
    const int statusH = 24;
    const int contentTop = top + btnH + margin;
    const int contentBottom = rc.bottom - statusH - margin;
    RECT split{};
    split.left = g_splitterX + 2;
    split.right = g_splitterX + 6;
    split.top = contentTop;
    split.bottom = contentBottom;
    return split;
}

static void ResizeChildren(HWND hwnd) {
    RECT rc{};
    GetClientRect(hwnd, &rc);
    const int margin = 8;
    const int top = 8;
    const int btnH = 30;
    const int statusH = 24;
    const int splitterW = 8;
    const int contentTop = top + btnH + margin;
    const int contentH = rc.bottom - contentTop - statusH - margin * 2;

    if (g_splitterX <= 0) g_splitterX = std::max<int>(300, static_cast<int>((rc.right - rc.left) / 3));
    g_splitterX = ClampSplitterX(g_splitterX, rc.right - rc.left);

    const int treeW = g_splitterX - margin;
    const int detailX = g_splitterX + splitterW + margin;
    const int detailW = rc.right - detailX - margin;

    MoveWindow(g_hRefresh, margin, top, 110, btnH, TRUE);
    MoveWindow(g_hExport, margin + 120, top, 90, btnH, TRUE);
    MoveWindow(g_hAutoMark, margin + 220, top + 5, 130, btnH - 8, TRUE);
    MoveWindow(g_hIntervalLabel, margin + 360, top + 6, 105, btnH - 8, TRUE);
    MoveWindow(g_hIntervalEdit, margin + 465, top + 3, 90, btnH - 6, TRUE);
    MoveWindow(g_hSearchEdit, margin + 565, top + 3, 180, btnH - 6, TRUE);
    MoveWindow(g_hSearchButton, margin + 755, top, 62, btnH, TRUE);
    MoveWindow(g_hSearchPrev, margin + 825, top, 30, btnH, TRUE);
    MoveWindow(g_hSearchNext, margin + 861, top, 30, btnH, TRUE);
    MoveWindow(g_hReverseLookup, margin + 899, top, 88, btnH, TRUE);
    MoveWindow(g_hTree, margin, contentTop, treeW, contentH, TRUE);
    MoveWindow(g_hDetail, detailX, contentTop, detailW, contentH, TRUE);
    MoveWindow(g_hStatus, margin, rc.bottom - statusH - margin / 2, rc.right - margin * 2, statusH, TRUE);
    InvalidateRect(hwnd, nullptr, TRUE);
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_TREEVIEW_CLASSES | ICC_STANDARD_CLASSES };
        InitCommonControlsEx(&icc);

        g_hRefresh = CreateWindowW(L"BUTTON", L"暂停刷新", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                  0, 0, 0, 0, hwnd, (HMENU)IDC_REFRESH, g_hInst, nullptr);
        g_hExport = CreateWindowW(L"BUTTON", L"导出", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                 0, 0, 0, 0, hwnd, (HMENU)IDC_EXPORT, g_hInst, nullptr);
        g_hAutoMark = CreateWindowExW(WS_EX_TRANSPARENT, L"BUTTON", L"预览时标注", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                   0, 0, 0, 0, hwnd, (HMENU)IDC_AUTOMARK, g_hInst, nullptr);
        g_hIntervalLabel = CreateWindowExW(WS_EX_TRANSPARENT, L"STATIC", L"刷新间隔(ms):", WS_CHILD | WS_VISIBLE | SS_LEFT,
                                      0, 0, 0, 0, hwnd, (HMENU)IDC_INTERVAL_LABEL, g_hInst, nullptr);
        g_hIntervalEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"1000",
                                        WS_CHILD | WS_VISIBLE | ES_NUMBER | ES_AUTOHSCROLL,
                                        0, 0, 0, 0, hwnd, (HMENU)IDC_INTERVAL_EDIT, g_hInst, nullptr);
        g_hSearchEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                        0, 0, 0, 0, hwnd, (HMENU)IDC_SEARCH_EDIT, g_hInst, nullptr);
        g_hSearchButton = CreateWindowW(L"BUTTON", L"搜索", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                        0, 0, 0, 0, hwnd, (HMENU)IDC_SEARCH_BUTTON, g_hInst, nullptr);
        g_hSearchPrev = CreateWindowW(L"BUTTON", L"←", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                      0, 0, 0, 0, hwnd, (HMENU)IDC_SEARCH_PREV, g_hInst, nullptr);
        g_hSearchNext = CreateWindowW(L"BUTTON", L"→", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                      0, 0, 0, 0, hwnd, (HMENU)IDC_SEARCH_NEXT, g_hInst, nullptr);
        g_hReverseLookup = CreateWindowW(L"BUTTON", L"鼠标反找", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                         0, 0, 0, 0, hwnd, (HMENU)IDC_REVERSE_LOOKUP, g_hInst, nullptr);
        SendMessageW(g_hSearchEdit, EM_SETCUEBANNER, TRUE, (LPARAM)L"输入关键字，先暂停刷新后搜索");
        g_hTree = CreateWindowExW(WS_EX_CLIENTEDGE, WC_TREEVIEWW, L"",
                                 WS_CHILD | WS_VISIBLE | TVS_HASBUTTONS | TVS_LINESATROOT | TVS_HASLINES | TVS_SHOWSELALWAYS,
                                 0, 0, 0, 0, hwnd, (HMENU)IDC_TREE, g_hInst, nullptr);
        g_hDetail = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                   WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL | ES_READONLY | WS_VSCROLL | WS_HSCROLL,
                                   0, 0, 0, 0, hwnd, (HMENU)IDC_DETAIL, g_hInst, nullptr);
        g_hStatus = CreateWindowExW(WS_EX_TRANSPARENT, L"STATIC", L"就绪。", WS_CHILD | WS_VISIBLE | SS_LEFT,
                                 0, 0, 0, 0, hwnd, (HMENU)IDC_STATUS, g_hInst, nullptr);

        HFONT font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        SendMessageW(g_hRefresh, WM_SETFONT, (WPARAM)font, TRUE);
        SendMessageW(g_hExport, WM_SETFONT, (WPARAM)font, TRUE);
        SendMessageW(g_hAutoMark, WM_SETFONT, (WPARAM)font, TRUE);
        SendMessageW(g_hIntervalLabel, WM_SETFONT, (WPARAM)font, TRUE);
        SendMessageW(g_hIntervalEdit, WM_SETFONT, (WPARAM)font, TRUE);
        SendMessageW(g_hSearchEdit, WM_SETFONT, (WPARAM)font, TRUE);
        SendMessageW(g_hSearchButton, WM_SETFONT, (WPARAM)font, TRUE);
        SendMessageW(g_hSearchPrev, WM_SETFONT, (WPARAM)font, TRUE);
        SendMessageW(g_hSearchNext, WM_SETFONT, (WPARAM)font, TRUE);
        SendMessageW(g_hReverseLookup, WM_SETFONT, (WPARAM)font, TRUE);
        SendMessageW(g_hTree, WM_SETFONT, (WPARAM)font, TRUE);
        SendMessageW(g_hDetail, WM_SETFONT, (WPARAM)font, TRUE);
        SendMessageW(g_hStatus, WM_SETFONT, (WPARAM)font, TRUE);

        RefreshWindowRoots();
        SetTimer(hwnd, IDT_AUTO_REFRESH, g_autoRefreshIntervalMs, nullptr);
        UpdateRefreshButtonText();
        UpdateSearchControlsEnabled();
        return 0;
    }
    case WM_SIZE:
        ResizeChildren(hwnd);
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT split = GetSplitterRect(hwnd);
        HBRUSH brush = CreateSolidBrush(g_dragSplitter ? RGB(120, 120, 120) : RGB(210, 210, 210));
        FillRect(hdc, &split, brush);
        DeleteObject(brush);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_SETCURSOR: {
        POINT pt{};
        GetCursorPos(&pt);
        ScreenToClient(hwnd, &pt);
        if (IsOnSplitter(pt.x)) {
            SetCursor(LoadCursor(nullptr, IDC_SIZEWE));
            return TRUE;
        }
        break;
    }
    case WM_LBUTTONDOWN: {
        int x = GET_X_LPARAM(lParam);
        if (IsOnSplitter(x)) {
            g_dragSplitter = true;
            SetCapture(hwnd);
            SetCursor(LoadCursor(nullptr, IDC_SIZEWE));
            return 0;
        }
        break;
    }
    case WM_MOUSEMOVE:
        if (g_dragSplitter) {
            RECT rc{};
            GetClientRect(hwnd, &rc);
            g_splitterX = ClampSplitterX(GET_X_LPARAM(lParam), rc.right - rc.left);
            ResizeChildren(hwnd);
            return 0;
        }
        break;
    case WM_LBUTTONUP:
        if (g_dragSplitter) {
            g_dragSplitter = false;
            ReleaseCapture();
            ResizeChildren(hwnd);
            return 0;
        }
        break;
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) {
            ClearAnnotation();
            return 0;
        }
        break;
    case WM_CTLCOLORBTN:
        if ((HWND)lParam == g_hAutoMark) {
            HDC hdc = (HDC)wParam;
            SetBkMode(hdc, TRANSPARENT);
            return (LRESULT)GetStockObject(HOLLOW_BRUSH);
        }
        break;
    case WM_CTLCOLORSTATIC:
        if ((HWND)lParam == g_hAutoMark ||
            (HWND)lParam == g_hIntervalLabel ||
            (HWND)lParam == g_hStatus) {
            HDC hdc = (HDC)wParam;
            SetBkMode(hdc, TRANSPARENT);
            return (LRESULT)GetStockObject(HOLLOW_BRUSH);
        }
        break;
    case WM_TIMER:
        if (wParam == IDT_AUTO_REFRESH) {
            if (!g_refreshPaused) RefreshWindows();
            return 0;
        }
        if (wParam == IDT_REVERSE_LOOKUP) {
            TrackReverseLookupMouse();
            return 0;
        }
        break;
    case WM_COMMAND:
        if (LOWORD(wParam) == IDC_REFRESH) {
            SetAutoRefreshPaused(hwnd, !g_refreshPaused);
            return 0;
        }
        if (LOWORD(wParam) == IDC_EXPORT) {
            ExportSelectedItems();
            return 0;
        }
        if (LOWORD(wParam) == IDC_REVERSE_LOOKUP && HIWORD(wParam) == BN_CLICKED) {
            ToggleReverseLookup(hwnd);
            return 0;
        }
        if (LOWORD(wParam) == IDC_SEARCH_BUTTON && HIWORD(wParam) == BN_CLICKED) {
            SearchFromCurrentSelection();
            return 0;
        }
        if (LOWORD(wParam) == IDC_SEARCH_PREV && HIWORD(wParam) == BN_CLICKED) {
            SearchPreviousResult();
            return 0;
        }
        if (LOWORD(wParam) == IDC_SEARCH_NEXT && HIWORD(wParam) == BN_CLICKED) {
            SearchNextResult();
            return 0;
        }
        if (LOWORD(wParam) == IDC_SEARCH_EDIT && HIWORD(wParam) == EN_CHANGE) {
            ClearSearchResults();
            return 0;
        }
        if (LOWORD(wParam) == IDC_INTERVAL_EDIT && HIWORD(wParam) == EN_KILLFOCUS) {
            ApplyRefreshIntervalFromEdit(hwnd);
            return 0;
        }
        if (LOWORD(wParam) == IDC_AUTOMARK && HIWORD(wParam) == BN_CLICKED) {
            g_autoAnnotate = IsAutoAnnotateChecked();
            if (g_autoAnnotate) {
                AutoAnnotateSelectedIfNeeded();
            } else {
                ClearAnnotation();
                SetStatusText( L"预览标注已关闭。");
            }
            return 0;
        }
        break;
    case WM_NOTIFY: {
        LPNMHDR hdr = reinterpret_cast<LPNMHDR>(lParam);
        if (hdr->idFrom == IDC_TREE) {
            if (hdr->code == NM_CUSTOMDRAW) {
                LPNMTVCUSTOMDRAW cd = reinterpret_cast<LPNMTVCUSTOMDRAW>(lParam);
                if (cd->nmcd.dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
                if (cd->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
                    HTREEITEM item = reinterpret_cast<HTREEITEM>(cd->nmcd.dwItemSpec);
                    NodeData* drawNode = GetNodeData(item);
                    if (drawNode && drawNode->disappeared) {
                        cd->clrText = RGB(128, 128, 128);
                        cd->clrTextBk = IsSelectedTreeItem(item) ? RGB(220, 220, 220) : RGB(255, 255, 255);
                    } else if (IsSelectedTreeItem(item)) {
                        cd->clrText = (drawNode && drawNode->changed) ? RGB(255, 226, 180) : RGB(255, 255, 255);
                        cd->clrTextBk = RGB(0, 120, 215);
                    } else if (drawNode && drawNode->changed) {
                        cd->clrText = RGB(180, 90, 0);
                        cd->clrTextBk = RGB(255, 230, 190);
                    }
                    return CDRF_DODEFAULT;
                }
            }
            if (hdr->code == NM_CLICK) {
                if (GetKeyState(VK_CONTROL) < 0) {
                    DWORD pos = GetMessagePos();
                    POINT screenPt{ GET_X_LPARAM(pos), GET_Y_LPARAM(pos) };
                    POINT clientPt = screenPt;
                    ScreenToClient(g_hTree, &clientPt);
                    TVHITTESTINFO hti{};
                    hti.pt = clientPt;
                    HTREEITEM item = TreeView_HitTest(g_hTree, &hti);
                    if (item && (hti.flags & (TVHT_ONITEM | TVHT_ONITEMICON | TVHT_ONITEMLABEL))) {
                        g_ctrlMultiClick = true;
                        ToggleSelectedItem(item);
                        if (IsSelectedTreeItem(item)) {
                            TreeView_SelectItem(g_hTree, item);
                        } else if (!g_selectedItems.empty()) {
                            TreeView_SelectItem(g_hTree, g_selectedItems.back());
                        } else {
                            TreeView_SelectItem(g_hTree, nullptr);
                        }
                        g_ctrlMultiClick = false;
                        NodeData* nd = GetNodeData(item);
                        if (nd && nd->kind == NodeData::Kind::Window) ShowWindowDetail(nd);
                        else if (nd && nd->kind == NodeData::Kind::Element) {
                            if (nd->disappeared) SetWindowTextW(g_hDetail, L"界面自动化元素\r\n状态：已消失（保留记录）\r\n");
                            else ShowElementDetail(nd->element);
                        }
                        AutoAnnotateSelectedIfNeeded();
                        return 1;
                    }
                }
            }
            if (hdr->code == NM_RCLICK) {
                ShowTreeContextMenu();
                return 0;
            }
            if (hdr->code == TVN_ITEMEXPANDINGW) {
                auto* nmtv = reinterpret_cast<NMTREEVIEWW*>(lParam);
                if (nmtv->action == TVE_EXPAND) {
                    NodeData* nd = GetNodeData(nmtv->itemNew.hItem);
                    if (nd && !nd->loaded && !nd->disappeared) {
                        nd->loaded = true;
                        if (nd->kind == NodeData::Kind::Window) {
                            SetStatusText( L"正在扫描选中窗口……");
                            LoadWindowChildren(nmtv->itemNew.hItem, nd);
                            SetStatusText( L"就绪。");
                        } else if (nd->kind == NodeData::Kind::Element) {
                            SetStatusText( L"正在加载子元素……");
                            LoadElementChildren(nmtv->itemNew.hItem, nd->element);
                            SetStatusText( L"就绪。");
                        }
                    }
                }
                return 0;
            }
            if (hdr->code == TVN_SELCHANGEDW) {
                auto* nmtv = reinterpret_cast<NMTREEVIEWW*>(lParam);
                NodeData* nd = GetNodeData(nmtv->itemNew.hItem);
                if (!nd) return 0;
                if (!g_ctrlMultiClick) SetSingleSelectedItem(nmtv->itemNew.hItem);
                if (nd->kind == NodeData::Kind::Window) ShowWindowDetail(nd);
                else if (nd->kind == NodeData::Kind::Element) {
                    if (nd->disappeared) SetWindowTextW(g_hDetail, L"界面自动化元素\r\n状态：已消失（保留记录）\r\n");
                    else ShowElementDetail(nd->element);
                } else SetWindowTextW(g_hDetail, L"");
                AutoAnnotateSelectedIfNeeded();
                return 0;
            }
        }
        break;
    }
    case WM_APP_REVERSE_LOOKUP_STOP:
        return 0;
    case WM_DESTROY:
        StopReverseLookup(nullptr);
        KillTimer(hwnd, IDT_AUTO_REFRESH);
        ClearAnnotation();
        for (HWND& h : g_hOverlay) {
            if (h && IsWindow(h)) DestroyWindow(h);
            h = nullptr;
        }
        if (g_hOverlayBrush) {
            DeleteObject(g_hOverlayBrush);
            g_hOverlayBrush = nullptr;
        }
        FreeNodes();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow) {
    g_hInst = hInstance;
    g_hOverlayBrush = CreateSolidBrush(RGB(255, 64, 64));
    RegisterOverlayClass();

    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(hr)) {
        MessageBoxW(nullptr, L"初始化组件失败。", L"杰睿元素树分析工具", MB_ICONERROR);
        return 1;
    }

    hr = CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER,
                          IID_IUIAutomation, reinterpret_cast<void**>(&g_uia));
    if (FAILED(hr) || !g_uia) {
        MessageBoxW(nullptr, L"创建界面自动化实例失败。", L"杰睿元素树分析工具", MB_ICONERROR);
        CoUninitialize();
        return 1;
    }

    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"JeriUiaExplorerWindow";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassW(&wc);

    g_hMain = CreateWindowExW(0, wc.lpszClassName, L"杰睿元素树分析工具",
                             WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1100, 720,
                             nullptr, nullptr, hInstance, nullptr);
    if (!g_hMain) {
        if (g_controlWalker) {
            g_controlWalker->Release();
            g_controlWalker = nullptr;
        }
        g_uia->Release();
        CoUninitialize();
        return 1;
    }

    ShowWindow(g_hMain, nCmdShow);
    UpdateWindow(g_hMain);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (g_controlWalker) {
        g_controlWalker->Release();
        g_controlWalker = nullptr;
    }
    if (g_uia) g_uia->Release();
    CoUninitialize();
    return static_cast<int>(msg.wParam);
}
