#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <commctrl.h>
#include <vector>
#include <string>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "msimg32.lib")

#define IDC_TREE       1001
#define IDC_REFRESH    1002
#define IDC_ANALYZE    1003
#define IDC_ANNOTATE   1004
#define IDC_STATUS     1005

struct NodeData {
    int type; // 0 window, 1 analysis root, 2 region
    HWND hwnd;
    int regionIndex;
};

enum RegionKind {
    REGION_KIND_SCENE = 0,
    REGION_KIND_BLOCK = 1,
    REGION_KIND_GROUP = 2,
    REGION_KIND_DETAIL = 3,
    REGION_KIND_TEXT = 4,
    REGION_KIND_ICON = 5,
    REGION_KIND_IMAGE = 6,
    REGION_KIND_TEXT_BLOCK = 7
};

struct Region {
    RECT rc; // relative to captured image
    double confidence;
    int kind;
    int parent;
    int level;
    int childCount;
};

struct TextLineCandidate {
    RECT rc;
    int pieceCount;
};

struct TextCluster {
    RECT block;
    std::vector<RECT> lines;
    int pieceCount;
};

static HINSTANCE gInst = NULL;
static HWND gMain = NULL;
static HWND gOverlay = NULL;
static HWND gTree = NULL;
static HWND gBtnRefresh = NULL;
static HWND gBtnAnalyze = NULL;
static HWND gChkAnnotate = NULL;
static HWND gStatus = NULL;
static int gLeftWidth = 330;
static HWND gSelectedHwnd = NULL;
static int gSelectedRegion = -1;
static bool gScreenAnnotate = true;
static std::vector<NodeData*> gNodeStore;
static std::vector<Region> gRegions;
static std::vector<std::vector<int> > gRegionChildren;
static std::vector<unsigned char> gPixels; // BGRA top-down
static int gImgW = 0;
static int gImgH = 0;
static RECT gCaptureScreenRect = {0,0,0,0};
static RECT gPreviewRect = {0,0,0,0};
static double gPreviewScale = 1.0;
static int gPreviewX = 0;
static int gPreviewY = 0;

// Final visual-DOM tuning constants. They keep large/noisy screenshots responsive
// without changing the public UI behaviour.
static const int kMaxCoarseBlocks = 110;
static const int kMaxSceneRanges = 32;
static const int kMaxVisualDomNodes = 620;

// Analysis cache: rebuilt once per screenshot and reused by every scoring/classification pass.
// It removes repeated full-rectangle pixel scans while keeping the same visual result.
static std::vector<int> gEdgeIntegral;
static int gEdgeIntegralW = 0;
static int gEdgeIntegralH = 0;

#ifndef DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
DECLARE_HANDLE(DPI_AWARENESS_CONTEXT);
#define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 ((DPI_AWARENESS_CONTEXT)-4)
#endif

typedef BOOL (WINAPI *PFN_SetProcessDpiAwarenessContext)(DPI_AWARENESS_CONTEXT);

typedef BOOL (WINAPI *PFN_SetProcessDPIAware)(void);

static void EnableDpiAwareness() {
    HMODULE user32 = LoadLibraryW(L"user32.dll");
    if (user32) {
        PFN_SetProcessDpiAwarenessContext p = (PFN_SetProcessDpiAwarenessContext)GetProcAddress(user32, "SetProcessDpiAwarenessContext");
        if (p && p(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {
            FreeLibrary(user32);
            return;
        }
        PFN_SetProcessDPIAware p2 = (PFN_SetProcessDPIAware)GetProcAddress(user32, "SetProcessDPIAware");
        if (p2) p2();
        FreeLibrary(user32);
    }
}

static RECT MakeRect(int l, int t, int r, int b) {
    RECT rc; rc.left = l; rc.top = t; rc.right = r; rc.bottom = b; return rc;
}

static int RectWidth(const RECT& r) { return r.right - r.left; }
static int RectHeight(const RECT& r) { return r.bottom - r.top; }

static int RectArea(const RECT& r) {
    int w = RectWidth(r);
    int h = RectHeight(r);
    return (w > 0 && h > 0) ? w * h : 0;
}

static bool IsValidWindowForList(HWND hwnd) {
    if (!IsWindow(hwnd)) return false;
    if (!IsWindowVisible(hwnd)) return false;
    if (hwnd == gMain || hwnd == gOverlay) return false;
    RECT rc;
    if (!GetWindowRect(hwnd, &rc)) return false;
    if (RectWidth(rc) < 80 || RectHeight(rc) < 60) return false;
    wchar_t title[512];
    title[0] = 0;
    GetWindowTextW(hwnd, title, 511);
    if (!title[0]) return false;
    return true;
}

static NodeData* NewNode(int type, HWND hwnd, int regionIndex) {
    NodeData* p = new NodeData();
    p->type = type;
    p->hwnd = hwnd;
    p->regionIndex = regionIndex;
    gNodeStore.push_back(p);
    return p;
}

static std::wstring GetWindowTextSafe(HWND hwnd) {
    wchar_t title[512];
    title[0] = 0;
    GetWindowTextW(hwnd, title, 511);
    wchar_t cls[256];
    cls[0] = 0;
    GetClassNameW(hwnd, cls, 255);
    std::wstring t = title[0] ? title : L"(无标题)";
    std::wstring c = cls[0] ? cls : L"(无类名)";
    return t + L"  [" + c + L"]";
}

static void RedrawTextControl(HWND ctrl) {
    if (!gMain || !ctrl) return;

    RECT rc;
    if (GetWindowRect(ctrl, &rc)) {
        MapWindowPoints(HWND_DESKTOP, gMain, (POINT*)&rc, 2);
        InvalidateRect(gMain, &rc, TRUE);
        RedrawWindow(gMain, &rc, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW);
    }

    InvalidateRect(ctrl, NULL, TRUE);
    RedrawWindow(ctrl, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW);
}

static void SetStatusText(const std::wstring& text) {
    if (!gStatus) return;
    RedrawTextControl(gStatus);
    SetWindowTextW(gStatus, text.c_str());
    RedrawTextControl(gStatus);
}

static HTREEITEM TreeInsert(HTREEITEM parent, const std::wstring& text, NodeData* data) {
    TVINSERTSTRUCTW ins;
    ZeroMemory(&ins, sizeof(ins));
    ins.hParent = parent;
    ins.hInsertAfter = TVI_LAST;
    ins.item.mask = TVIF_TEXT | TVIF_PARAM;
    ins.item.pszText = const_cast<LPWSTR>(text.c_str());
    ins.item.lParam = (LPARAM)data;
    return TreeView_InsertItem(gTree, &ins);
}

static void RefreshOverlay();

static BOOL CALLBACK EnumTopProc(HWND hwnd, LPARAM) {
    if (!IsValidWindowForList(hwnd)) return TRUE;
    TreeInsert(TVI_ROOT, GetWindowTextSafe(hwnd), NewNode(0, hwnd, -1));
    return TRUE;
}

static void ClearAnalysis() {
    gRegions.clear();
    gRegionChildren.clear();
    gPixels.clear();
    gImgW = 0;
    gImgH = 0;
    gCaptureScreenRect = MakeRect(0,0,0,0);
    gSelectedRegion = -1;
}

static void RefreshWindows() {
    TreeView_DeleteAllItems(gTree);
    ClearAnalysis();
    gSelectedHwnd = NULL;
    EnumWindows(EnumTopProc, 0);
    SetStatusText(L"窗口列表已刷新。请选择左侧窗口，然后点击“分析”。");
    InvalidateRect(gMain, NULL, TRUE);
    RefreshOverlay();
}

static bool CaptureWindowImage(HWND hwnd) {
    gPixels.clear();
    gImgW = gImgH = 0;
    gEdgeIntegral.clear();
    gEdgeIntegralW = gEdgeIntegralH = 0;
    gCaptureScreenRect = MakeRect(0,0,0,0);
    if (!IsWindow(hwnd)) return false;
    RECT rc;
    if (!GetWindowRect(hwnd, &rc)) return false;
    int w = RectWidth(rc);
    int h = RectHeight(rc);
    if (w <= 0 || h <= 0 || w > 12000 || h > 12000) return false;

    HDC screen = GetDC(NULL);
    HDC mem = CreateCompatibleDC(screen);
    BITMAPINFO bi;
    ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h; // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = NULL;
    HBITMAP dib = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!dib || !bits) {
        if (dib) DeleteObject(dib);
        DeleteDC(mem);
        ReleaseDC(NULL, screen);
        return false;
    }
    HGDIOBJ old = SelectObject(mem, dib);
    BOOL ok = BitBlt(mem, 0, 0, w, h, screen, rc.left, rc.top, SRCCOPY | CAPTUREBLT);
    SelectObject(mem, old);
    if (!ok) {
        DeleteObject(dib);
        DeleteDC(mem);
        ReleaseDC(NULL, screen);
        return false;
    }
    gPixels.resize((size_t)w * h * 4);
    memcpy(gPixels.data(), bits, gPixels.size());
    gImgW = w;
    gImgH = h;
    gCaptureScreenRect = rc;
    DeleteObject(dib);
    DeleteDC(mem);
    ReleaseDC(NULL, screen);
    return true;
}

static int ClampInt(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static double ClampDouble(double v, double lo, double hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static bool IntersectsInflated(const RECT& a, const RECT& b, int pad) {
    RECT x = a;
    x.left -= pad; x.top -= pad; x.right += pad; x.bottom += pad;
    return !(x.right < b.left || x.left > b.right || x.bottom < b.top || x.top > b.bottom);
}

static RECT UnionRect2(const RECT& a, const RECT& b) {
    return MakeRect(std::min(a.left,b.left), std::min(a.top,b.top), std::max(a.right,b.right), std::max(a.bottom,b.bottom));
}

static RECT IntersectRect2(const RECT& a, const RECT& b) {
    return MakeRect(std::max(a.left,b.left), std::max(a.top,b.top), std::min(a.right,b.right), std::min(a.bottom,b.bottom));
}

static RECT InflateRectClamped(const RECT& r, int pad, int w, int h) {
    return MakeRect(ClampInt(r.left - pad, 0, w), ClampInt(r.top - pad, 0, h),
                    ClampInt(r.right + pad, 0, w), ClampInt(r.bottom + pad, 0, h));
}

static int RangeOverlap(int a1, int a2, int b1, int b2) {
    int v = std::min(a2, b2) - std::max(a1, b1);
    return v > 0 ? v : 0;
}

static int RectGapX(const RECT& a, const RECT& b) {
    if (a.right < b.left) return b.left - a.right;
    if (b.right < a.left) return a.left - b.right;
    return 0;
}

static int RectGapY(const RECT& a, const RECT& b) {
    if (a.bottom < b.top) return b.top - a.bottom;
    if (b.bottom < a.top) return a.top - b.bottom;
    return 0;
}

static double RectIoU(const RECT& a, const RECT& b) {
    RECT in = IntersectRect2(a, b);
    int ia = RectArea(in);
    if (ia <= 0) return 0.0;
    int ua = RectArea(a) + RectArea(b) - ia;
    if (ua <= 0) return 0.0;
    return (double)ia / (double)ua;
}

static bool ShouldMergeRegions(const RECT& a, const RECT& b, int maxArea) {
    RECT u = UnionRect2(a, b);
    int ua = RectArea(u);
    if (ua <= 0 || ua > maxArea) return false;

    int aw = RectWidth(a), ah = RectHeight(a);
    int bw = RectWidth(b), bh = RectHeight(b);
    int minW = std::max(1, std::min(aw, bw));
    int minH = std::max(1, std::min(ah, bh));
    int maxW = std::max(aw, bw);
    int maxH = std::max(ah, bh);
    int gapX = RectGapX(a, b);
    int gapY = RectGapY(a, b);

    if (gapX == 0 && gapY == 0) return true;
    if (IntersectsInflated(a, b, 2)) return true;

    int ovY = RangeOverlap(a.top, a.bottom, b.top, b.bottom);
    int ovX = RangeOverlap(a.left, a.right, b.left, b.right);
    double verticalOverlap = (double)ovY / (double)minH;
    double horizontalOverlap = (double)ovX / (double)minW;

    // Same row: merge broken text/icon strokes inside one visual control,
    // but avoid turning a whole toolbar into one giant rectangle.
    if (verticalOverlap >= 0.58 && gapX <= std::max(5, minH / 3)) {
        if (RectHeight(u) <= maxH + std::max(8, minH / 2) && RectWidth(u) <= maxW + gapX + std::max(80, maxW / 2)) {
            return true;
        }
    }

    // Same column: merge border fragments that belong to the same field/list item.
    if (horizontalOverlap >= 0.60 && gapY <= std::max(4, minW / 6)) {
        if (RectWidth(u) <= maxW + std::max(8, minW / 2) && RectHeight(u) <= maxH + gapY + std::max(60, maxH / 2)) {
            return true;
        }
    }

    return false;
}

// Binary dilation implemented with two sliding-window passes.
// Compared with the old nested-neighbour dilation, this is faster on large windows
// and less likely to glue unrelated controls together.
static void Dilate(std::vector<unsigned char>& img, int w, int h, int radiusX, int radiusY, int times) {
    if (w <= 0 || h <= 0 || img.empty()) return;
    radiusX = std::max(0, radiusX);
    radiusY = std::max(0, radiusY);
    std::vector<unsigned char> tmp(img.size());
    std::vector<unsigned char> out(img.size());

    for (int it = 0; it < times; ++it) {
        if (radiusX > 0) {
            std::fill(tmp.begin(), tmp.end(), 0);
            for (int y = 0; y < h; ++y) {
                int count = 0;
                int row = y * w;
                int firstRight = std::min(w - 1, radiusX);
                for (int xx = 0; xx <= firstRight; ++xx) if (img[row + xx]) ++count;
                for (int x = 0; x < w; ++x) {
                    tmp[row + x] = count > 0 ? 1 : 0;
                    int rem = x - radiusX;
                    int add = x + radiusX + 1;
                    if (rem >= 0 && img[row + rem]) --count;
                    if (add < w && img[row + add]) ++count;
                }
            }
        } else {
            tmp = img;
        }

        if (radiusY > 0) {
            std::fill(out.begin(), out.end(), 0);
            for (int x = 0; x < w; ++x) {
                int count = 0;
                int firstBottom = std::min(h - 1, radiusY);
                for (int yy = 0; yy <= firstBottom; ++yy) if (tmp[yy * w + x]) ++count;
                for (int y = 0; y < h; ++y) {
                    out[y * w + x] = count > 0 ? 1 : 0;
                    int rem = y - radiusY;
                    int add = y + radiusY + 1;
                    if (rem >= 0 && tmp[rem * w + x]) --count;
                    if (add < h && tmp[add * w + x]) ++count;
                }
            }
            img.swap(out);
        } else {
            img.swap(tmp);
        }
    }
}

static int LocalContrastAt(const std::vector<unsigned char>& gray, int w, int h, int x, int y) {
    int lo = 255;
    int hi = 0;
    for (int yy = y - 1; yy <= y + 1; ++yy) {
        if (yy < 0 || yy >= h) continue;
        for (int xx = x - 1; xx <= x + 1; ++xx) {
            if (xx < 0 || xx >= w) continue;
            int v = gray[yy * w + xx];
            if (v < lo) lo = v;
            if (v > hi) hi = v;
        }
    }
    return hi - lo;
}

static int EdgeStrengthAt(const std::vector<unsigned char>& gray, int w, int h, int x, int y) {
    if (x <= 0 || y <= 0 || x >= w - 1 || y >= h - 1) return 0;

    int p00 = gray[(y-1)*w + x-1], p01 = gray[(y-1)*w + x], p02 = gray[(y-1)*w + x+1];
    int p10 = gray[y*w + x-1],     p12 = gray[y*w + x+1];
    int p20 = gray[(y+1)*w + x-1], p21 = gray[(y+1)*w + x], p22 = gray[(y+1)*w + x+1];
    int gx = -p00 - 2*p10 - p20 + p02 + 2*p12 + p22;
    int gy = -p00 - 2*p01 - p02 + p20 + 2*p21 + p22;
    int grayMag = std::abs(gx) + std::abs(gy);

    size_t l = ((size_t)y * w + (x - 1)) * 4;
    size_t r = ((size_t)y * w + (x + 1)) * 4;
    size_t t = ((size_t)(y - 1) * w + x) * 4;
    size_t b = ((size_t)(y + 1) * w + x) * 4;
    int dxB = std::abs((int)gPixels[r + 0] - (int)gPixels[l + 0]);
    int dxG = std::abs((int)gPixels[r + 1] - (int)gPixels[l + 1]);
    int dxR = std::abs((int)gPixels[r + 2] - (int)gPixels[l + 2]);
    int dyB = std::abs((int)gPixels[b + 0] - (int)gPixels[t + 0]);
    int dyG = std::abs((int)gPixels[b + 1] - (int)gPixels[t + 1]);
    int dyR = std::abs((int)gPixels[b + 2] - (int)gPixels[t + 2]);
    int maxDx = std::max(dxB, std::max(dxG, dxR));
    int maxDy = std::max(dyB, std::max(dyG, dyR));
    int colorMag = (maxDx + maxDy) * 4;

    return std::max(grayMag, colorMag);
}

static void BuildEdgeIntegral(const std::vector<unsigned char>& rawEdge, int w, int h) {
    gEdgeIntegral.clear();
    gEdgeIntegralW = w;
    gEdgeIntegralH = h;
    if (w <= 0 || h <= 0 || rawEdge.empty()) return;

    gEdgeIntegral.assign((size_t)(w + 1) * (h + 1), 0);
    for (int y = 0; y < h; ++y) {
        int rowSum = 0;
        int dstRow = (y + 1) * (w + 1);
        int prevRow = y * (w + 1);
        int srcRow = y * w;
        for (int x = 0; x < w; ++x) {
            rowSum += rawEdge[srcRow + x] ? 1 : 0;
            gEdgeIntegral[dstRow + x + 1] = gEdgeIntegral[prevRow + x + 1] + rowSum;
        }
    }
}

static int CountEdgeIntegralRect(const RECT& rc, int w, int h) {
    if (gEdgeIntegral.empty() || gEdgeIntegralW != w || gEdgeIntegralH != h) return -1;
    RECT r = InflateRectClamped(rc, 0, w, h);
    if (RectWidth(r) <= 0 || RectHeight(r) <= 0) return 0;
    int stride = w + 1;
    int a = gEdgeIntegral[r.top * stride + r.left];
    int b = gEdgeIntegral[r.top * stride + r.right];
    int c = gEdgeIntegral[r.bottom * stride + r.left];
    int d = gEdgeIntegral[r.bottom * stride + r.right];
    return d - b - c + a;
}

static double EstimateEdgeDensity(const std::vector<unsigned char>& rawEdge, int w, int h, const RECT& rc) {
    RECT r = InflateRectClamped(rc, 0, w, h);
    int rw = RectWidth(r);
    int rh = RectHeight(r);
    int area = rw * rh;
    if (area <= 0) return 0.0;

    int fast = CountEdgeIntegralRect(r, w, h);
    if (fast >= 0) return (double)fast / (double)area;

    int stepX = std::max(1, rw / 160);
    int stepY = std::max(1, rh / 120);
    int hit = 0;
    int total = 0;
    for (int y = r.top; y < r.bottom; y += stepY) {
        for (int x = r.left; x < r.right; x += stepX) {
            ++total;
            if (rawEdge[y * w + x]) ++hit;
        }
    }
    return total > 0 ? (double)hit / (double)total : 0.0;
}

static double ScoreRegion(const std::vector<unsigned char>& rawEdge, int w, int h, const RECT& r) {
    int rw = RectWidth(r);
    int rh = RectHeight(r);
    int area = RectArea(r);
    double areaRatio = (double)area / (double)std::max(1, w * h);
    double density = EstimateEdgeDensity(rawEdge, w, h, r);

    double score = 0.32 + ClampDouble(density * 3.2, 0.0, 0.32);
    if (rw >= 18 && rh >= 10) score += 0.08;
    if (rh >= 14 && rh <= 96 && rw >= rh * 2) score += 0.18;  // button/text-field/list-row like
    if (rw >= 80 && rh >= 50 && areaRatio <= 0.22) score += 0.08; // small panel/card
    if (areaRatio > 0.55) score -= 0.25;
    if (rw <= 5 || rh <= 5) score -= 0.20;
    return ClampDouble(score, 0.10, 0.98);
}


static int CountRawEdgePixels(const std::vector<unsigned char>& rawEdge, int w, int h, const RECT& rc) {
    int fast = CountEdgeIntegralRect(rc, w, h);
    if (fast >= 0) return fast;

    RECT r = InflateRectClamped(rc, 0, w, h);
    int total = 0;
    for (int y = r.top; y < r.bottom; ++y) {
        int row = y * w;
        for (int x = r.left; x < r.right; ++x) {
            if (rawEdge[row + x]) ++total;
        }
    }
    return total;
}

static double InnerEdgeRatio(const std::vector<unsigned char>& rawEdge, int w, int h, const RECT& rc) {
    int total = CountRawEdgePixels(rawEdge, w, h, rc);
    if (total <= 0) return 0.0;

    int rw = RectWidth(rc);
    int rh = RectHeight(rc);
    int shrinkX = ClampInt(rw / 8, 1, 4);
    int shrinkY = ClampInt(rh / 8, 1, 4);
    if (rw <= shrinkX * 2 + 1 || rh <= shrinkY * 2 + 1) return 1.0;

    RECT inner = MakeRect(rc.left + shrinkX, rc.top + shrinkY, rc.right - shrinkX, rc.bottom - shrinkY);
    int innerCount = CountRawEdgePixels(rawEdge, w, h, inner);
    return (double)innerCount / (double)total;
}

static bool IsLikelyTextPiece(const std::vector<unsigned char>& rawEdge, int w, int h, const RECT& rc) {
    int rw = RectWidth(rc);
    int rh = RectHeight(rc);
    int area = RectArea(rc);
    if (rw < 2 || rh < 5 || area <= 0) return false;
    if (rh > 56) return false;
    if (rw > std::max(260, w * 82 / 100)) return false;
    if (area > std::max(9000, w * h / 18)) return false;
    if (rh <= 4 && rw > 24) return false;
    if (rw <= 3 && rh > 24) return false;
    if (rw > rh * 32) return false;

    int edgeCount = CountRawEdgePixels(rawEdge, w, h, rc);
    if (edgeCount < std::max(3, area / 90)) return false;
    double density = (double)edgeCount / (double)area;
    if (density < 0.018 || density > 0.72) return false;

    // Text strokes usually exist inside the bounding box. A pure button/input border
    // mostly has edges on the outside, so avoid treating it as text during this pass.
    double innerRatio = InnerEdgeRatio(rawEdge, w, h, rc);
    if (area > 380 && innerRatio < 0.11) return false;
    if (rw >= 70 && rh >= 18 && innerRatio < 0.18) return false;
    return true;
}


static double EstimateColorVarianceInRect(int w, int h, const RECT& rc) {
    RECT r = InflateRectClamped(rc, 0, w, h);
    int rw = RectWidth(r);
    int rh = RectHeight(r);
    if (rw <= 0 || rh <= 0 || gPixels.empty()) return 0.0;

    int stepX = std::max(1, rw / 48);
    int stepY = std::max(1, rh / 40);
    double sum = 0.0;
    double sum2 = 0.0;
    int count = 0;
    for (int y = r.top; y < r.bottom; y += stepY) {
        for (int x = r.left; x < r.right; x += stepX) {
            size_t p = ((size_t)y * w + x) * 4;
            int b = gPixels[p + 0];
            int g = gPixels[p + 1];
            int rr = gPixels[p + 2];
            int luma = (rr * 30 + g * 59 + b * 11) / 100;
            sum += luma;
            sum2 += (double)luma * luma;
            ++count;
        }
    }
    if (count <= 1) return 0.0;
    double mean = sum / count;
    double var = sum2 / count - mean * mean;
    return var > 0.0 ? var : 0.0;
}

static double EstimateColorRichnessInRect(int w, int h, const RECT& rc) {
    RECT r = InflateRectClamped(rc, 0, w, h);
    int rw = RectWidth(r);
    int rh = RectHeight(r);
    if (rw <= 0 || rh <= 0 || gPixels.empty()) return 0.0;

    unsigned char bins[64];
    ZeroMemory(bins, sizeof(bins));
    int stepX = std::max(1, rw / 42);
    int stepY = std::max(1, rh / 34);
    int samples = 0;
    for (int y = r.top; y < r.bottom; y += stepY) {
        for (int x = r.left; x < r.right; x += stepX) {
            size_t p = ((size_t)y * w + x) * 4;
            int b = gPixels[p + 0] >> 6;
            int g = gPixels[p + 1] >> 6;
            int rr = gPixels[p + 2] >> 6;
            bins[(rr << 4) | (g << 2) | b] = 1;
            ++samples;
        }
    }
    if (samples <= 0) return 0.0;
    int used = 0;
    for (int i = 0; i < 64; ++i) if (bins[i]) ++used;
    return (double)used / 64.0;
}

static bool IsLikelyImageElement(const std::vector<unsigned char>& rawEdge, int w, int h, const RECT& rc) {
    int rw = RectWidth(rc);
    int rh = RectHeight(rc);
    int area = RectArea(rc);
    if (rw < 34 || rh < 28 || area < 1100) return false;
    if (rw > w * 96 / 100 && rh > h * 82 / 100) return false;
    if (rw > rh * 9 || rh > rw * 9) return false;

    double density = EstimateEdgeDensity(rawEdge, w, h, rc);
    double variance = EstimateColorVarianceInRect(w, h, rc);
    double richness = EstimateColorRichnessInRect(w, h, rc);
    double innerRatio = InnerEdgeRatio(rawEdge, w, h, rc);

    // Photos, thumbnails, charts and preview images usually have rich colour/gray changes
    // distributed inside the region. Pure buttons and input fields mostly have border edges.
    if (variance >= 720.0 && richness >= 0.16 && innerRatio >= 0.22) return true;
    if (area >= 4200 && variance >= 430.0 && richness >= 0.12 && density >= 0.045) return true;
    if (area >= 9000 && richness >= 0.20 && innerRatio >= 0.16) return true;
    return false;
}

static bool IsLikelyIconElement(const std::vector<unsigned char>& rawEdge, int w, int h, const RECT& rc) {
    int rw = RectWidth(rc);
    int rh = RectHeight(rc);
    int area = RectArea(rc);
    if (rw < 8 || rh < 8 || area < 64) return false;
    if (rw > 72 || rh > 72 || area > 5200) return false;

    int minSide = std::max(1, std::min(rw, rh));
    int maxSide = std::max(rw, rh);
    if (maxSide > minSide * 3) return false;

    int edgeCount = CountRawEdgePixels(rawEdge, w, h, rc);
    if (edgeCount < std::max(4, area / 80)) return false;
    double density = (double)edgeCount / (double)area;
    if (density < 0.025 || density > 0.62) return false;

    double innerRatio = InnerEdgeRatio(rawEdge, w, h, rc);
    double richness = EstimateColorRichnessInRect(w, h, rc);
    if (innerRatio < 0.16) return false;
    if (richness > 0.30 && area > 900) return false; // more likely a thumbnail/image than an icon
    return true;
}

static int ClassifyElementRegion(const std::vector<unsigned char>& rawEdge, int w, int h, const RECT& rc) {
    if (IsLikelyImageElement(rawEdge, w, h, rc)) return REGION_KIND_IMAGE;

    bool textLike = IsLikelyTextPiece(rawEdge, w, h, rc);
    bool iconLike = IsLikelyIconElement(rawEdge, w, h, rc);
    int rw = RectWidth(rc);
    int rh = RectHeight(rc);
    int area = RectArea(rc);
    bool compactSquare = rw <= rh * 2 && rh <= rw * 2 && area <= 2600;

    // Small square visual marks are more useful to developers as icons.
    // Wider same-baseline fragments are kept as text.
    if (iconLike && (!textLike || compactSquare)) return REGION_KIND_ICON;
    if (textLike) return REGION_KIND_TEXT;
    if (iconLike) return REGION_KIND_ICON;
    return REGION_KIND_DETAIL;
}

static bool ShouldMergeTextPieces(const std::vector<unsigned char>& rawEdge, int w, int h, const RECT& a, const RECT& b) {
    if (!IsLikelyTextPiece(rawEdge, w, h, a) || !IsLikelyTextPiece(rawEdge, w, h, b)) return false;

    RECT u = UnionRect2(a, b);
    int uw = RectWidth(u);
    int uh = RectHeight(u);
    if (uw <= 0 || uh <= 0) return false;
    if (uh > 64 || uw > std::max(520, w * 86 / 100)) return false;

    int aw = RectWidth(a), ah = RectHeight(a);
    int bw = RectWidth(b), bh = RectHeight(b);
    int minW = std::max(1, std::min(aw, bw));
    int minH = std::max(1, std::min(ah, bh));
    int maxH = std::max(ah, bh);
    int gapX = RectGapX(a, b);
    int gapY = RectGapY(a, b);
    int ovY = RangeOverlap(a.top, a.bottom, b.top, b.bottom);
    int ovX = RangeOverlap(a.left, a.right, b.left, b.right);
    double yOverlap = (double)ovY / (double)minH;
    double xOverlap = (double)ovX / (double)minW;
    int cyA = (a.top + a.bottom) / 2;
    int cyB = (b.top + b.bottom) / 2;
    int cxA = (a.left + a.right) / 2;
    int cxB = (b.left + b.right) / 2;

    // Main text-line rule: merge neighbouring glyphs/words on the same baseline.
    // The gap is intentionally larger than the general region merge, but only for
    // rectangles that look like text pieces.
    bool sameBaseline = (yOverlap >= 0.36) || (std::abs(cyA - cyB) <= std::max(4, minH / 2));
    int textGapX = std::max(10, std::min(18, minH + 3));
    if (sameBaseline && gapX > 0 && gapX <= textGapX && gapY <= std::max(2, minH / 3)) {
        if (uh <= maxH + std::max(7, minH * 2 / 3)) return true;
    }

    // Broken glyph rule: merge upper/lower or left/right stroke fragments inside
    // one Chinese character or one letter, without connecting different rows.
    bool sameColumn = (xOverlap >= 0.34) || (std::abs(cxA - cxB) <= std::max(4, minW / 2));
    int textGapY = std::max(3, std::min(8, minH));
    if (sameColumn && gapY > 0 && gapY <= textGapY && gapX <= std::max(3, minW / 2)) {
        if (uw <= std::max(90, std::max(aw, bw) + minW * 2) && uh <= std::max(42, maxH + minH + gapY)) return true;
    }

    return false;
}

static void MergeTextFragments(std::vector<RECT>& rects, const std::vector<unsigned char>& rawEdge, int w, int h) {
    bool changed = true;
    int round = 0;
    while (changed && round < 14) {
        changed = false;
        ++round;
        for (size_t i = 0; i < rects.size() && !changed; ++i) {
            for (size_t j = i + 1; j < rects.size(); ++j) {
                if (ShouldMergeTextPieces(rawEdge, w, h, rects[i], rects[j])) {
                    rects[i] = UnionRect2(rects[i], rects[j]);
                    rects[j] = rects.back();
                    rects.pop_back();
                    changed = true;
                    break;
                }
            }
        }
    }
}

static void RemoveNearDuplicateRects(std::vector<RECT>& rects) {
    std::sort(rects.begin(), rects.end(), [](const RECT& a, const RECT& b){
        int aa = RectArea(a);
        int ab = RectArea(b);
        if (aa != ab) return aa < ab;
        if (a.top != b.top) return a.top < b.top;
        return a.left < b.left;
    });

    std::vector<RECT> clean;
    for (size_t i = 0; i < rects.size(); ++i) {
        bool dup = false;
        int area = RectArea(rects[i]);
        for (size_t j = 0; j < clean.size(); ++j) {
            double iou = RectIoU(rects[i], clean[j]);
            RECT in = IntersectRect2(rects[i], clean[j]);
            int ia = RectArea(in);
            int ca = RectArea(clean[j]);
            if (iou >= 0.76 || (area > 0 && ca > 0 && ia >= (int)(std::min(area, ca) * 0.90) && std::max(area, ca) <= std::min(area, ca) * 1.25)) {
                dup = true;
                break;
            }
        }
        if (!dup) clean.push_back(rects[i]);
    }
    rects.swap(clean);
}


static void SortRectsReadingOrder(std::vector<RECT>& rects) {
    std::sort(rects.begin(), rects.end(), [](const RECT& a, const RECT& b){
        int rowA = a.top / 16;
        int rowB = b.top / 16;
        if (rowA != rowB) return rowA < rowB;
        if (a.top != b.top) return a.top < b.top;
        return a.left < b.left;
    });
}

static bool RegionReadingLess(const Region& a, const Region& b) {
    int rowA = a.rc.top / 16;
    int rowB = b.rc.top / 16;
    if (rowA != rowB) return rowA < rowB;
    if (a.rc.top != b.rc.top) return a.rc.top < b.rc.top;
    if (a.rc.left != b.rc.left) return a.rc.left < b.rc.left;
    return RectArea(a.rc) > RectArea(b.rc);
}

static bool MostlyContainsRect(const RECT& outer, const RECT& inner) {
    int innerArea = RectArea(inner);
    int outerArea = RectArea(outer);
    if (innerArea <= 0 || outerArea <= innerArea) return false;

    RECT in = IntersectRect2(outer, inner);
    int inArea = RectArea(in);
    if (inArea <= 0) return false;

    int ow = RectWidth(outer);
    int oh = RectHeight(outer);
    int tol = ClampInt(std::min(ow, oh) / 45, 2, 10);
    bool boxContains = outer.left <= inner.left + tol && outer.top <= inner.top + tol &&
                       outer.right >= inner.right - tol && outer.bottom >= inner.bottom - tol;
    double insideRatio = (double)inArea / (double)innerArea;
    return boxContains || insideRatio >= 0.90;
}

static bool RegionCanBeParent(const Region& r) {
    if (r.kind != REGION_KIND_SCENE && r.kind != REGION_KIND_BLOCK && r.kind != REGION_KIND_GROUP && r.kind != REGION_KIND_TEXT_BLOCK) return false;
    int rw = RectWidth(r.rc);
    int rh = RectHeight(r.rc);
    int area = RectArea(r.rc);
    if (rw < 18 || rh < 14 || area < 420) return false;
    if (rh <= 8 && rw > 120) return false;
    if (rw <= 8 && rh > 120) return false;
    return true;
}

static int RegionKindPriority(int kind) {
    if (kind == REGION_KIND_SCENE) return 8;
    if (kind == REGION_KIND_BLOCK) return 7;
    if (kind == REGION_KIND_GROUP) return 6;
    if (kind == REGION_KIND_TEXT_BLOCK) return 5;
    if (kind == REGION_KIND_IMAGE) return 4;
    if (kind == REGION_KIND_ICON) return 3;
    if (kind == REGION_KIND_TEXT) return 2;
    return 1;
}

static const wchar_t* RegionKindName(int kind) {
    if (kind == REGION_KIND_SCENE) return L"大场景";
    if (kind == REGION_KIND_BLOCK) return L"范围";
    if (kind == REGION_KIND_GROUP) return L"小场景";
    if (kind == REGION_KIND_TEXT_BLOCK) return L"文本块";
    if (kind == REGION_KIND_TEXT) return L"文本";
    if (kind == REGION_KIND_ICON) return L"图标";
    if (kind == REGION_KIND_IMAGE) return L"图片";
    return L"元素";
}

static double RectInsideRatio(const RECT& outer, const RECT& inner) {
    int innerArea = RectArea(inner);
    if (innerArea <= 0) return 0.0;
    RECT in = IntersectRect2(outer, inner);
    int inArea = RectArea(in);
    if (inArea <= 0) return 0.0;
    return (double)inArea / (double)innerArea;
}

static bool RectCenterInside(const RECT& outer, const RECT& inner) {
    int cx = (inner.left + inner.right) / 2;
    int cy = (inner.top + inner.bottom) / 2;
    return cx >= outer.left && cx <= outer.right && cy >= outer.top && cy <= outer.bottom;
}

static bool RectCouldContainWithTolerance(const RECT& outer, const RECT& inner, int tol) {
    if (outer.left <= inner.left + tol && outer.top <= inner.top + tol &&
        outer.right >= inner.right - tol && outer.bottom >= inner.bottom - tol) {
        return true;
    }
    return RectCenterInside(outer, inner);
}

static bool IsUsefulLayoutGroupRect(const RECT& groupRc, const RECT& blockRc, int w, int h, bool rowGroup) {
    int gw = RectWidth(groupRc);
    int gh = RectHeight(groupRc);
    int ga = RectArea(groupRc);
    int ba = RectArea(blockRc);
    if (gw < 22 || gh < 10 || ga < 180) return false;
    if (ba > 0 && ga >= ba * 92 / 100) return false;
    if (RectIoU(groupRc, blockRc) >= 0.82) return false;

    if (rowGroup) {
        int maxRowH = std::max(48, std::min(110, RectHeight(blockRc) / 3 + 24));
        if (gh > maxRowH) return false;
        if (gw > std::max(80, RectWidth(blockRc) - 2) && gh > RectHeight(blockRc) / 2) return false;
    } else {
        int maxColW = std::max(80, std::min(420, RectWidth(blockRc) * 76 / 100));
        if (gw > maxColW && RectWidth(blockRc) > 180) return false;
        if (gh < std::max(52, RectHeight(blockRc) / 5)) return false;
    }

    if (gw > w * 98 / 100 && gh > h * 98 / 100) return false;
    return true;
}

static bool RegionRectDuplicate(const std::vector<Region>& regions, const RECT& rc, int kind) {
    int area = RectArea(rc);
    if (area <= 0) return true;
    for (size_t i = 0; i < regions.size(); ++i) {
        int otherArea = RectArea(regions[i].rc);
        if (otherArea <= 0) continue;
        double iou = RectIoU(rc, regions[i].rc);
        RECT in = IntersectRect2(rc, regions[i].rc);
        int ia = RectArea(in);
        int small = std::min(area, otherArea);
        int large = std::max(area, otherArea);
        if (regions[i].kind == kind && (iou >= 0.72 || (ia >= small * 90 / 100 && large <= small * 13 / 10))) {
            return true;
        }
        if (kind == REGION_KIND_GROUP && (regions[i].kind == REGION_KIND_BLOCK || regions[i].kind == REGION_KIND_SCENE) && iou >= 0.76) {
            return true;
        }
        if (kind == REGION_KIND_SCENE && regions[i].kind == REGION_KIND_SCENE && iou >= 0.70) {
            return true;
        }
    }
    return false;
}


static bool SameTextLineBand(const RECT& line, const RECT& r) {
    int minH = std::max(1, std::min(RectHeight(line), RectHeight(r)));
    int ovY = RangeOverlap(line.top, line.bottom, r.top, r.bottom);
    int cyLine = (line.top + line.bottom) / 2;
    int cyR = (r.top + r.bottom) / 2;
    return ovY >= minH * 34 / 100 || std::abs(cyLine - cyR) <= std::max(6, minH / 2 + 2);
}

static bool TextLineDuplicate(const std::vector<TextLineCandidate>& lines, const RECT& rc) {
    int area = RectArea(rc);
    if (area <= 0) return true;
    for (size_t i = 0; i < lines.size(); ++i) {
        int otherArea = RectArea(lines[i].rc);
        if (otherArea <= 0) continue;
        RECT in = IntersectRect2(rc, lines[i].rc);
        int ia = RectArea(in);
        int small = std::min(area, otherArea);
        int large = std::max(area, otherArea);
        if (RectIoU(rc, lines[i].rc) >= 0.72 || (small > 0 && ia >= small * 88 / 100 && large <= small * 14 / 10)) return true;
    }
    return false;
}

static std::vector<TextLineCandidate> BuildTextLineCandidates(const std::vector<RECT>& rects,
                                                              const std::vector<unsigned char>& rawEdge,
                                                              int w, int h) {
    std::vector<RECT> pieces;
    for (size_t i = 0; i < rects.size(); ++i) {
        if (!IsLikelyTextPiece(rawEdge, w, h, rects[i])) continue;
        int rw = RectWidth(rects[i]);
        int rh = RectHeight(rects[i]);
        int area = RectArea(rects[i]);
        if (area <= 0) continue;
        if (rw > w * 92 / 100 || rh > 72) continue;
        pieces.push_back(rects[i]);
    }
    if (pieces.empty()) return std::vector<TextLineCandidate>();

    std::sort(pieces.begin(), pieces.end(), [](const RECT& a, const RECT& b){
        int ca = (a.top + a.bottom) / 2;
        int cb = (b.top + b.bottom) / 2;
        if (ca != cb) return ca < cb;
        return a.left < b.left;
    });

    struct WorkLine {
        RECT rc;
        std::vector<RECT> parts;
    };
    std::vector<WorkLine> bands;

    for (size_t i = 0; i < pieces.size(); ++i) {
        int best = -1;
        int bestDist = 0x7fffffff;
        int cy = (pieces[i].top + pieces[i].bottom) / 2;
        for (size_t b = 0; b < bands.size(); ++b) {
            if (!SameTextLineBand(bands[b].rc, pieces[i])) continue;
            int bcy = (bands[b].rc.top + bands[b].rc.bottom) / 2;
            int dist = std::abs(cy - bcy);
            if (dist < bestDist) {
                best = (int)b;
                bestDist = dist;
            }
        }
        if (best >= 0) {
            bands[best].rc = UnionRect2(bands[best].rc, pieces[i]);
            bands[best].parts.push_back(pieces[i]);
        } else {
            WorkLine wl;
            wl.rc = pieces[i];
            wl.parts.push_back(pieces[i]);
            bands.push_back(wl);
        }
    }

    std::vector<TextLineCandidate> lines;
    for (size_t b = 0; b < bands.size(); ++b) {
        std::vector<RECT>& parts = bands[b].parts;
        std::sort(parts.begin(), parts.end(), [](const RECT& a, const RECT& b){
            if (a.left != b.left) return a.left < b.left;
            return a.top < b.top;
        });

        RECT seg = parts[0];
        int segCount = 1;
        int heightSum = RectHeight(parts[0]);
        for (size_t i = 1; i < parts.size(); ++i) {
            int hAvg = std::max(1, heightSum / std::max(1, segCount));
            int gap = RectGapX(seg, parts[i]);
            int splitGap = ClampInt(std::max(34, hAvg * 4), 34, 86);
            bool sameLine = SameTextLineBand(seg, parts[i]);
            if (!sameLine || gap > splitGap) {
                int sw = RectWidth(seg);
                int sh = RectHeight(seg);
                if (segCount >= 2 || (sw >= 42 && sh >= 7)) {
                    TextLineCandidate lc;
                    lc.rc = InflateRectClamped(seg, 1, w, h);
                    lc.pieceCount = segCount;
                    if (!TextLineDuplicate(lines, lc.rc)) lines.push_back(lc);
                }
                seg = parts[i];
                segCount = 1;
                heightSum = RectHeight(parts[i]);
            } else {
                seg = UnionRect2(seg, parts[i]);
                ++segCount;
                heightSum += RectHeight(parts[i]);
            }
        }
        int sw = RectWidth(seg);
        int sh = RectHeight(seg);
        if (segCount >= 2 || (sw >= 42 && sh >= 7)) {
            TextLineCandidate lc;
            lc.rc = InflateRectClamped(seg, 1, w, h);
            lc.pieceCount = segCount;
            if (!TextLineDuplicate(lines, lc.rc)) lines.push_back(lc);
        }
    }

    std::sort(lines.begin(), lines.end(), [](const TextLineCandidate& a, const TextLineCandidate& b){
        if (a.rc.top != b.rc.top) return a.rc.top < b.rc.top;
        return a.rc.left < b.rc.left;
    });
    return lines;
}


static bool ShouldAttachTextLineToCluster(const TextCluster& cluster, const TextLineCandidate& line) {
    RECT c = cluster.block;
    RECT r = line.rc;
    int gapY = RectGapY(c, r);
    int lineH = std::max(1, RectHeight(r));
    int clusterH = std::max(1, RectHeight(c));
    int allowedGap = ClampInt(std::max(16, std::min(lineH, clusterH) * 2 + 8), 18, 46);
    if (gapY > allowedGap) return false;

    int minW = std::max(1, std::min(RectWidth(c), RectWidth(r)));
    int maxW = std::max(RectWidth(c), RectWidth(r));
    int ovX = RangeOverlap(c.left, c.right, r.left, r.right);
    double xOverlap = (double)ovX / (double)minW;
    int leftDiff = std::abs(c.left - r.left);
    int rightDiff = std::abs(c.right - r.right);
    int centerDiff = std::abs((c.left + c.right) / 2 - (r.left + r.right) / 2);

    bool aligned = leftDiff <= std::max(32, lineH * 3) ||
                   rightDiff <= std::max(36, lineH * 3) ||
                   centerDiff <= std::max(44, maxW / 6) ||
                   xOverlap >= 0.22;
    bool bothLong = RectWidth(c) >= 86 && RectWidth(r) >= 86 && gapY <= std::max(30, lineH * 2 + 8);
    bool paragraphLike = RectWidth(r) >= 52 && RectWidth(c) >= 52 &&
                         (leftDiff <= std::max(42, lineH * 4) || xOverlap >= 0.18) &&
                         gapY <= std::max(28, lineH * 2 + 6);
    return aligned || bothLong || paragraphLike;
}

static int CountColumnEdgePixelsInBand(const std::vector<unsigned char>& rawEdge, int w, int h, int x, int top, int bottom) {
    if (x < 0 || x >= w) return 0;
    top = ClampInt(top, 0, h);
    bottom = ClampInt(bottom, 0, h);
    if (bottom <= top) return 0;

    int fast = CountEdgeIntegralRect(MakeRect(x, top, x + 1, bottom), w, h);
    if (fast >= 0) return fast;

    int count = 0;
    for (int y = top; y < bottom; ++y) {
        if (rawEdge[y * w + x]) ++count;
    }
    return count;
}

static bool IsProjectionTextLineShape(const std::vector<unsigned char>& rawEdge, int w, int h, const RECT& rc) {
    int rw = RectWidth(rc);
    int rh = RectHeight(rc);
    int area = RectArea(rc);
    if (rw < 16 || rh < 5 || area <= 0) return false;
    if (rh > 42) return false;
    if (rw > w * 96 / 100 && rh > 22) return false;

    int edgeCount = CountRawEdgePixels(rawEdge, w, h, rc);
    if (edgeCount < std::max(5, rw / 12)) return false;
    double density = (double)edgeCount / (double)area;
    if (density < 0.006 || density > 0.62) return false;

    // Reject pure separator lines: they are very flat and have little vertical structure.
    if (rh <= 4 && rw > 60) return false;
    int activeCols = 0;
    int colRuns = 0;
    bool inRun = false;
    for (int x = rc.left; x < rc.right; ++x) {
        int c = CountColumnEdgePixelsInBand(rawEdge, w, h, x, rc.top, rc.bottom);
        bool active = c > 0;
        if (active) {
            ++activeCols;
            if (!inRun) {
                ++colRuns;
                inRun = true;
            }
        } else {
            inRun = false;
        }
    }
    if (activeCols < std::max(4, rw / 18)) return false;
    if (rw > 70 && colRuns < 3) return false;
    return true;
}

static std::vector<TextLineCandidate> BuildProjectionTextLineCandidates(const std::vector<unsigned char>& rawEdge, int w, int h) {
    std::vector<TextLineCandidate> lines;
    if (rawEdge.empty() || w <= 0 || h <= 0) return lines;

    std::vector<int> rowHits(h, 0);
    for (int y = 0; y < h; ++y) {
        int fast = CountEdgeIntegralRect(MakeRect(0, y, w, y + 1), w, h);
        if (fast >= 0) {
            rowHits[y] = fast;
        } else {
            int row = y * w;
            int c = 0;
            for (int x = 0; x < w; ++x) if (rawEdge[row + x]) ++c;
            rowHits[y] = c;
        }
    }

    int minRowHits = std::max(3, w / 420);
    int maxRowHits = std::max(18, w * 58 / 100);
    int y = 0;
    while (y < h) {
        while (y < h && (rowHits[y] < minRowHits || rowHits[y] > maxRowHits)) ++y;
        if (y >= h) break;
        int top = y;
        int last = y;
        int gap = 0;
        ++y;
        while (y < h) {
            bool active = rowHits[y] >= minRowHits && rowHits[y] <= maxRowHits;
            if (active) {
                last = y;
                gap = 0;
            } else if (gap <= 2) {
                ++gap;
            } else {
                break;
            }
            ++y;
        }
        int bottom = last + 1;
        int bandH = bottom - top;
        if (bandH < 5 || bandH > 42) continue;

        std::vector<int> colHits(w, 0);
        for (int x = 0; x < w; ++x) {
            int c = 0;
            int x0 = std::max(0, x - 2);
            int x1 = std::min(w - 1, x + 2);
            for (int xx = x0; xx <= x1; ++xx) c += CountColumnEdgePixelsInBand(rawEdge, w, h, xx, top, bottom);
            colHits[x] = c;
        }

        int minColHits = std::max(1, bandH / 5);
        int x = 0;
        std::vector<RECT> runs;
        while (x < w) {
            while (x < w && colHits[x] < minColHits) ++x;
            if (x >= w) break;
            int left = x;
            int lastX = x;
            int xgap = 0;
            ++x;
            while (x < w) {
                if (colHits[x] >= minColHits) {
                    lastX = x;
                    xgap = 0;
                } else if (xgap <= 4) {
                    ++xgap;
                } else {
                    break;
                }
                ++x;
            }
            int right = lastX + 1;
            if (right - left >= 2) runs.push_back(MakeRect(left, top, right, bottom));
        }

        if (runs.empty()) continue;
        std::vector<RECT> merged;
        RECT cur = runs[0];
        for (size_t i = 1; i < runs.size(); ++i) {
            int gapX = RectGapX(cur, runs[i]);
            int joinGap = ClampInt(std::max(18, bandH * 3), 22, 62);
            if (gapX <= joinGap) {
                cur = UnionRect2(cur, runs[i]);
            } else {
                merged.push_back(cur);
                cur = runs[i];
            }
        }
        merged.push_back(cur);

        for (size_t i = 0; i < merged.size(); ++i) {
            RECT rc = InflateRectClamped(merged[i], 1, w, h);
            if (!IsProjectionTextLineShape(rawEdge, w, h, rc)) continue;
            if (TextLineDuplicate(lines, rc)) continue;
            TextLineCandidate lc;
            lc.rc = rc;
            lc.pieceCount = std::max(2, RectWidth(rc) / std::max(8, RectHeight(rc) / 2));
            lines.push_back(lc);
        }
    }

    std::sort(lines.begin(), lines.end(), [](const TextLineCandidate& a, const TextLineCandidate& b){
        if (a.rc.top != b.rc.top) return a.rc.top < b.rc.top;
        return a.rc.left < b.rc.left;
    });
    return lines;
}

static bool IsLikelyGlyphFragment(const std::vector<unsigned char>& rawEdge, int w, int h, const RECT& rc) {
    if (IsLikelyTextPiece(rawEdge, w, h, rc)) return true;
    int rw = RectWidth(rc);
    int rh = RectHeight(rc);
    int area = RectArea(rc);
    if (rw < 2 || rh < 5 || area <= 0) return false;
    if (rw > 76 || rh > 62 || area > 2600) return false;
    if (rw <= 4 && rh > 34) return false;
    if (rh <= 4 && rw > 34) return false;

    int edgeCount = CountRawEdgePixels(rawEdge, w, h, rc);
    if (edgeCount < std::max(3, area / 110)) return false;
    double density = (double)edgeCount / (double)area;
    if (density < 0.012 || density > 0.76) return false;
    double innerRatio = InnerEdgeRatio(rawEdge, w, h, rc);
    if (innerRatio < 0.08 && area > 240) return false;
    return true;
}


static std::vector<TextCluster> BuildTextClusters(const std::vector<RECT>& rects,
                                                  const std::vector<unsigned char>& rawEdge,
                                                  int w, int h) {
    std::vector<TextLineCandidate> lines = BuildTextLineCandidates(rects, rawEdge, w, h);
    std::vector<TextLineCandidate> projectionLines = BuildProjectionTextLineCandidates(rawEdge, w, h);
    for (size_t i = 0; i < projectionLines.size(); ++i) {
        if (!TextLineDuplicate(lines, projectionLines[i].rc)) lines.push_back(projectionLines[i]);
    }
    if (lines.empty()) return std::vector<TextCluster>();

    std::sort(lines.begin(), lines.end(), [](const TextLineCandidate& a, const TextLineCandidate& b){
        if (a.rc.top != b.rc.top) return a.rc.top < b.rc.top;
        return a.rc.left < b.rc.left;
    });

    std::vector<TextCluster> clusters;
    for (size_t i = 0; i < lines.size(); ++i) {
        int best = -1;
        int bestArea = 0x7fffffff;
        for (size_t c = 0; c < clusters.size(); ++c) {
            if (!ShouldAttachTextLineToCluster(clusters[c], lines[i])) continue;
            RECT u = UnionRect2(clusters[c].block, lines[i].rc);
            int area = RectArea(u);
            if (area < bestArea) {
                best = (int)c;
                bestArea = area;
            }
        }
        if (best >= 0) {
            clusters[best].block = UnionRect2(clusters[best].block, lines[i].rc);
            clusters[best].lines.push_back(lines[i].rc);
            clusters[best].pieceCount += lines[i].pieceCount;
        } else {
            TextCluster tc;
            tc.block = lines[i].rc;
            tc.lines.push_back(lines[i].rc);
            tc.pieceCount = lines[i].pieceCount;
            clusters.push_back(tc);
        }
    }

    // Merge paragraph fragments once more. Projection-based lines can split one
    // paragraph into several islands when there is an icon, link, or big word gap.
    bool changed = true;
    int round = 0;
    while (changed && round < 6) {
        changed = false;
        ++round;
        for (size_t i = 0; i < clusters.size() && !changed; ++i) {
            for (size_t j = i + 1; j < clusters.size(); ++j) {
                TextLineCandidate probe;
                probe.rc = clusters[j].block;
                probe.pieceCount = clusters[j].pieceCount;
                if (!ShouldAttachTextLineToCluster(clusters[i], probe)) continue;
                RECT merged = UnionRect2(clusters[i].block, clusters[j].block);
                if (RectArea(merged) > std::max(1, w * h) * 58 / 100) continue;
                clusters[i].block = merged;
                clusters[i].pieceCount += clusters[j].pieceCount;
                clusters[i].lines.insert(clusters[i].lines.end(), clusters[j].lines.begin(), clusters[j].lines.end());
                clusters[j] = clusters.back();
                clusters.pop_back();
                changed = true;
                break;
            }
        }
    }

    std::vector<TextCluster> kept;
    int fullArea = std::max(1, w * h);
    for (size_t i = 0; i < clusters.size(); ++i) {
        clusters[i].block = InflateRectClamped(clusters[i].block, 2, w, h);
        int bw = RectWidth(clusters[i].block);
        int bh = RectHeight(clusters[i].block);
        int ba = RectArea(clusters[i].block);
        if (ba < 72 || ba > fullArea * 58 / 100) continue;
        bool paragraph = clusters[i].lines.size() >= 2;
        bool denseLine = clusters[i].pieceCount >= 4;
        bool longLine = bw >= 72 && bh <= 82 && clusters[i].pieceCount >= 2;
        bool compactCaption = bw >= 40 && bh <= 34 && CountRawEdgePixels(rawEdge, w, h, clusters[i].block) >= 8;
        if (!paragraph && !denseLine && !longLine && !compactCaption) continue;
        bool dup = false;
        for (size_t k = 0; k < kept.size(); ++k) {
            RECT in = IntersectRect2(clusters[i].block, kept[k].block);
            int small = std::min(RectArea(clusters[i].block), RectArea(kept[k].block));
            if (RectIoU(clusters[i].block, kept[k].block) >= 0.66 || (small > 0 && RectArea(in) >= small * 86 / 100)) {
                kept[k].block = UnionRect2(kept[k].block, clusters[i].block);
                kept[k].pieceCount += clusters[i].pieceCount;
                kept[k].lines.insert(kept[k].lines.end(), clusters[i].lines.begin(), clusters[i].lines.end());
                dup = true;
                break;
            }
        }
        if (!dup) kept.push_back(clusters[i]);
    }

    if (kept.size() > 100) {
        std::sort(kept.begin(), kept.end(), [](const TextCluster& a, const TextCluster& b){
            if (a.lines.size() != b.lines.size()) return a.lines.size() > b.lines.size();
            return RectArea(a.block) > RectArea(b.block);
        });
        kept.resize(100);
    }

    std::sort(kept.begin(), kept.end(), [](const TextCluster& a, const TextCluster& b){
        if (a.block.top != b.block.top) return a.block.top < b.block.top;
        return a.block.left < b.block.left;
    });
    return kept;
}


static bool IsTextRectCoveredByCluster(const RECT& rc,
                                       const std::vector<TextCluster>& clusters,
                                       const std::vector<unsigned char>& rawEdge,
                                       int w, int h) {
    if (!IsLikelyGlyphFragment(rawEdge, w, h, rc)) return false;
    int area = RectArea(rc);
    if (area <= 0) return false;
    for (size_t i = 0; i < clusters.size(); ++i) {
        int ca = RectArea(clusters[i].block);
        if (ca <= 0) continue;
        double inside = RectInsideRatio(clusters[i].block, rc);
        bool center = RectCenterInside(clusters[i].block, rc);
        if (inside < 0.78 && !center) continue;
        if (clusters[i].lines.size() >= 2) return true;
        if (clusters[i].pieceCount >= 3 && area < ca * 94 / 100) return true;
        if (RectIoU(rc, clusters[i].block) >= 0.68) return true;
    }
    return false;
}

static void SuppressTextPiecesCoveredByClusters(std::vector<RECT>& rects,
                                                const std::vector<TextCluster>& clusters,
                                                const std::vector<unsigned char>& rawEdge,
                                                int w, int h) {
    if (clusters.empty() || rects.empty()) return;
    std::vector<RECT> kept;
    kept.reserve(rects.size());
    for (size_t i = 0; i < rects.size(); ++i) {
        if (IsTextRectCoveredByCluster(rects[i], clusters, rawEdge, w, h)) continue;
        kept.push_back(rects[i]);
    }
    rects.swap(kept);
}

static void AppendLayoutGroup(std::vector<Region>& additions,
                              const std::vector<Region>& baseRegions,
                              const RECT& rc,
                              const std::vector<unsigned char>& rawEdge,
                              int w, int h,
                              bool rowGroup) {
    RECT groupRc = InflateRectClamped(rc, rowGroup ? 3 : 4, w, h);
    if (RegionRectDuplicate(baseRegions, groupRc, REGION_KIND_GROUP)) return;
    if (RegionRectDuplicate(additions, groupRc, REGION_KIND_GROUP)) return;

    Region reg;
    reg.rc = groupRc;
    reg.confidence = ClampDouble(ScoreRegion(rawEdge, w, h, groupRc) * 0.86 + (rowGroup ? 0.08 : 0.05), 0.20, 0.94);
    reg.kind = REGION_KIND_GROUP;
    reg.parent = -1;
    reg.level = 0;
    reg.childCount = 0;
    additions.push_back(reg);
}

static void AddRowLayoutGroupsForBlock(std::vector<Region>& additions,
                                        const std::vector<Region>& regions,
                                        const std::vector<int>& items,
                                        const RECT& blockRc,
                                        const std::vector<unsigned char>& rawEdge,
                                        int w, int h) {
    if (items.size() < 2) return;

    std::vector<int> order = items;
    std::sort(order.begin(), order.end(), [&regions](int a, int b) {
        int ca = (regions[a].rc.top + regions[a].rc.bottom) / 2;
        int cb = (regions[b].rc.top + regions[b].rc.bottom) / 2;
        if (ca != cb) return ca < cb;
        return regions[a].rc.left < regions[b].rc.left;
    });

    std::vector<unsigned char> used(order.size(), 0);
    for (size_t i = 0; i < order.size(); ++i) {
        if (used[i]) continue;
        RECT line = regions[order[i]].rc;
        int maxItemH = RectHeight(line);
        std::vector<size_t> picked;
        picked.push_back(i);

        for (size_t j = i + 1; j < order.size(); ++j) {
            if (used[j]) continue;
            RECT r = regions[order[j]].rc;
            int minH = std::max(1, std::min(RectHeight(line), RectHeight(r)));
            int ovY = RangeOverlap(line.top, line.bottom, r.top, r.bottom);
            int cyLine = (line.top + line.bottom) / 2;
            int cyR = (r.top + r.bottom) / 2;
            bool sameLine = ovY >= minH * 36 / 100 || std::abs(cyLine - cyR) <= std::max(7, minH / 2 + 2);
            RECT next = UnionRect2(line, r);
            int maxAllowedH = std::max(44, std::min(110, std::max(maxItemH, RectHeight(r)) * 3));
            if (sameLine && RectHeight(next) <= maxAllowedH) {
                line = next;
                maxItemH = std::max(maxItemH, RectHeight(r));
                picked.push_back(j);
            }
        }

        if (picked.size() >= 2 && IsUsefulLayoutGroupRect(line, blockRc, w, h, true)) {
            AppendLayoutGroup(additions, regions, line, rawEdge, w, h, true);
            for (size_t k = 0; k < picked.size(); ++k) used[picked[k]] = 1;
        }
    }
}

static void AddColumnLayoutGroupsForBlock(std::vector<Region>& additions,
                                           const std::vector<Region>& regions,
                                           const std::vector<int>& items,
                                           const RECT& blockRc,
                                           const std::vector<unsigned char>& rawEdge,
                                           int w, int h) {
    if (items.size() < 3) return;

    std::vector<int> order = items;
    std::sort(order.begin(), order.end(), [&regions](int a, int b) {
        if (regions[a].rc.left != regions[b].rc.left) return regions[a].rc.left < regions[b].rc.left;
        return regions[a].rc.top < regions[b].rc.top;
    });

    std::vector<unsigned char> used(order.size(), 0);
    for (size_t i = 0; i < order.size(); ++i) {
        if (used[i]) continue;
        RECT col = regions[order[i]].rc;
        int maxItemW = RectWidth(col);
        std::vector<size_t> picked;
        picked.push_back(i);

        for (size_t j = i + 1; j < order.size(); ++j) {
            if (used[j]) continue;
            RECT r = regions[order[j]].rc;
            int minW = std::max(1, std::min(RectWidth(col), RectWidth(r)));
            int ovX = RangeOverlap(col.left, col.right, r.left, r.right);
            int cxCol = (col.left + col.right) / 2;
            int cxR = (r.left + r.right) / 2;
            bool sameColumn = ovX >= minW * 52 / 100 ||
                              std::abs(cxCol - cxR) <= std::max(12, minW / 3);
            bool alignedEdge = std::abs(col.left - r.left) <= 16 || std::abs(col.right - r.right) <= 20;
            RECT next = UnionRect2(col, r);
            int maxAllowedW = std::max(72, std::min(420, std::max(maxItemW, RectWidth(r)) * 3));
            if ((sameColumn || alignedEdge) && RectWidth(next) <= maxAllowedW) {
                col = next;
                maxItemW = std::max(maxItemW, RectWidth(r));
                picked.push_back(j);
            }
        }

        if (picked.size() >= 3 && IsUsefulLayoutGroupRect(col, blockRc, w, h, false)) {
            AppendLayoutGroup(additions, regions, col, rawEdge, w, h, false);
            for (size_t k = 0; k < picked.size(); ++k) used[picked[k]] = 1;
        }
    }
}

static void AddSyntheticLayoutGroups(std::vector<Region>& regions,
                                      const std::vector<unsigned char>& rawEdge,
                                      int w, int h) {
    if (regions.empty()) return;
    std::vector<Region> additions;

    for (size_t bi = 0; bi < regions.size(); ++bi) {
        if (regions[bi].kind != REGION_KIND_BLOCK) continue;
        RECT blockRc = regions[bi].rc;
        int blockArea = RectArea(blockRc);
        if (blockArea < 800) continue;

        std::vector<int> items;
        for (size_t i = 0; i < regions.size(); ++i) {
            if (i == bi || regions[i].kind == REGION_KIND_SCENE || regions[i].kind == REGION_KIND_BLOCK || regions[i].kind == REGION_KIND_GROUP || regions[i].kind == REGION_KIND_TEXT) continue;
            RECT r = regions[i].rc;
            int area = RectArea(r);
            if (area <= 0 || area >= blockArea * 82 / 100) continue;
            if (RectInsideRatio(blockRc, r) < 0.84 && !RectCenterInside(blockRc, r)) continue;
            if (RectWidth(r) > RectWidth(blockRc) * 96 / 100 && RectHeight(r) > RectHeight(blockRc) * 70 / 100) continue;
            items.push_back((int)i);
        }

        AddRowLayoutGroupsForBlock(additions, regions, items, blockRc, rawEdge, w, h);
        AddColumnLayoutGroupsForBlock(additions, regions, items, blockRc, rawEdge, w, h);
    }

    if (!additions.empty()) {
        regions.insert(regions.end(), additions.begin(), additions.end());
    }
}

static bool PruneWeakGroupNodes(std::vector<Region>& regions) {
    bool changed = false;
    std::vector<Region> kept;
    kept.reserve(regions.size());
    for (size_t i = 0; i < regions.size(); ++i) {
        if (regions[i].kind == REGION_KIND_GROUP && regions[i].childCount < 2) {
            changed = true;
            continue;
        }
        int fullSceneLimit = std::max(1, (int)((long long)gImgW * gImgH * 95 / 100));
        if (regions[i].kind == REGION_KIND_SCENE && regions[i].childCount < 1 && RectArea(regions[i].rc) < fullSceneLimit) {
            changed = true;
            continue;
        }
        kept.push_back(regions[i]);
    }
    if (changed) regions.swap(kept);
    return changed;
}


static bool PruneOverDetailedVisualDomNodes(std::vector<Region>& regions,
                                            const std::vector<TextCluster>& clusters,
                                            const std::vector<unsigned char>& rawEdge,
                                            int w, int h) {
    if (regions.empty()) return false;
    std::vector<unsigned char> drop(regions.size(), 0);

    // Text blocks are containers. Any glyph-like leaf fully covered by them is
    // visual noise for a DOM tree and should not appear as one node per character.
    for (size_t i = 0; i < regions.size(); ++i) {
        if (regions[i].kind == REGION_KIND_TEXT_BLOCK || regions[i].kind == REGION_KIND_SCENE || regions[i].kind == REGION_KIND_BLOCK || regions[i].kind == REGION_KIND_GROUP) continue;
        for (size_t c = 0; c < clusters.size(); ++c) {
            if (RectInsideRatio(clusters[c].block, regions[i].rc) >= 0.78 || RectCenterInside(clusters[c].block, regions[i].rc)) {
                if (IsLikelyGlyphFragment(rawEdge, w, h, regions[i].rc) && RectArea(regions[i].rc) < RectArea(clusters[c].block) * 86 / 100) {
                    drop[i] = 1;
                    break;
                }
            }
        }
    }

    // If one container still has too many tiny direct leaves, keep the meaningful
    // nodes and collapse character-like leftovers into their parent.
    for (size_t p = 0; p < gRegionChildren.size(); ++p) {
        const std::vector<int>& ch = gRegionChildren[p];
        if (ch.size() <= 42) continue;
        int tinyCount = 0;
        int leafCount = 0;
        int parentArea = std::max(1, RectArea(regions[p].rc));
        int tinyLimit = std::max(90, parentArea / 360);
        for (size_t k = 0; k < ch.size(); ++k) {
            int idx = ch[k];
            if (idx < 0 || idx >= (int)regions.size()) continue;
            if (regions[idx].childCount > 0) continue;
            ++leafCount;
            int kind = regions[idx].kind;
            bool lowValueKind = kind == REGION_KIND_TEXT || kind == REGION_KIND_ICON || kind == REGION_KIND_DETAIL;
            if (lowValueKind && RectArea(regions[idx].rc) <= tinyLimit) ++tinyCount;
        }
        if (leafCount < 28 || tinyCount < 16) continue;
        int keepBudget = 28;
        for (size_t k = 0; k < ch.size(); ++k) {
            int idx = ch[k];
            if (idx < 0 || idx >= (int)regions.size()) continue;
            if (regions[idx].childCount > 0) continue;
            int kind = regions[idx].kind;
            bool lowValueKind = kind == REGION_KIND_TEXT || kind == REGION_KIND_ICON || kind == REGION_KIND_DETAIL;
            if (!lowValueKind) continue;
            if (RectArea(regions[idx].rc) <= tinyLimit || IsLikelyGlyphFragment(rawEdge, w, h, regions[idx].rc)) {
                if (keepBudget > 0 && regions[idx].confidence >= 0.82 && RectArea(regions[idx].rc) > tinyLimit / 2) {
                    --keepBudget;
                } else {
                    drop[idx] = 1;
                }
            }
        }
    }

    bool changed = false;
    std::vector<Region> kept;
    kept.reserve(regions.size());
    for (size_t i = 0; i < regions.size(); ++i) {
        if (drop[i]) {
            changed = true;
            continue;
        }
        kept.push_back(regions[i]);
    }
    if (changed) regions.swap(kept);
    return changed;
}

static void BuildRegionTree(std::vector<Region>& regions) {
    gRegionChildren.clear();
    if (regions.empty()) return;

    std::vector<int> areas(regions.size(), 0);
    std::vector<int> priorities(regions.size(), 0);
    std::vector<unsigned char> canParent(regions.size(), 0);
    for (size_t i = 0; i < regions.size(); ++i) {
        regions[i].parent = -1;
        regions[i].level = 0;
        regions[i].childCount = 0;
        areas[i] = RectArea(regions[i].rc);
        priorities[i] = RegionKindPriority(regions[i].kind);
        canParent[i] = RegionCanBeParent(regions[i]) ? 1 : 0;
    }

    // Parent inference is naturally O(n^2). Keep it cheap by caching area,
    // kind priority and parent eligibility instead of recomputing them in every
    // comparison.
    for (size_t i = 0; i < regions.size(); ++i) {
        int childArea = areas[i];
        int best = -1;
        int bestArea = 0x7fffffff;
        int bestPriority = -1;

        for (size_t j = 0; j < regions.size(); ++j) {
            if (i == j || !canParent[j]) continue;
            int parentArea = areas[j];
            if (parentArea <= childArea + std::max(24, childArea / 8)) continue;
            int tol = ClampInt(std::min(RectWidth(regions[j].rc), RectHeight(regions[j].rc)) / 45, 2, 10);
            if (!RectCouldContainWithTolerance(regions[j].rc, regions[i].rc, tol)) continue;
            if (RectIoU(regions[i].rc, regions[j].rc) >= 0.82) continue;
            if (!MostlyContainsRect(regions[j].rc, regions[i].rc)) continue;

            int priority = priorities[j];
            if (parentArea < bestArea || (parentArea == bestArea && priority > bestPriority)) {
                best = (int)j;
                bestArea = parentArea;
                bestPriority = priority;
            }
        }
        regions[i].parent = best;
    }

    std::vector<std::vector<int> > oldChildren(regions.size());
    std::vector<int> roots;
    for (size_t i = 0; i < regions.size(); ++i) {
        int p = regions[i].parent;
        if (p >= 0 && p < (int)regions.size()) {
            oldChildren[p].push_back((int)i);
        } else {
            regions[i].parent = -1;
            roots.push_back((int)i);
        }
    }

    auto sorter = [&regions](int a, int b) { return RegionReadingLess(regions[a], regions[b]); };
    std::sort(roots.begin(), roots.end(), sorter);
    for (size_t i = 0; i < oldChildren.size(); ++i) {
        std::sort(oldChildren[i].begin(), oldChildren[i].end(), sorter);
    }

    std::vector<int> order;
    order.reserve(regions.size());
    std::function<void(int)> visit = [&](int idx) {
        order.push_back(idx);
        for (size_t k = 0; k < oldChildren[idx].size(); ++k) visit(oldChildren[idx][k]);
    };
    for (size_t i = 0; i < roots.size(); ++i) visit(roots[i]);

    std::vector<int> mapOldToNew(regions.size(), -1);
    for (size_t i = 0; i < order.size(); ++i) mapOldToNew[order[i]] = (int)i;

    std::vector<Region> ordered;
    ordered.reserve(regions.size());
    for (size_t i = 0; i < order.size(); ++i) {
        Region r = regions[order[i]];
        r.parent = (r.parent >= 0 && r.parent < (int)mapOldToNew.size()) ? mapOldToNew[r.parent] : -1;
        r.level = 0;
        r.childCount = 0;
        ordered.push_back(r);
    }

    gRegionChildren.assign(ordered.size(), std::vector<int>());
    for (size_t i = 0; i < ordered.size(); ++i) {
        int p = ordered[i].parent;
        if (p >= 0 && p < (int)ordered.size()) {
            gRegionChildren[p].push_back((int)i);
        }
    }

    std::function<void(int,int)> setLevel = [&](int idx, int level) {
        ordered[idx].level = level;
        ordered[idx].childCount = (int)gRegionChildren[idx].size();
        for (size_t k = 0; k < gRegionChildren[idx].size(); ++k) {
            setLevel(gRegionChildren[idx][k], level + 1);
        }
    };
    for (size_t i = 0; i < ordered.size(); ++i) {
        if (ordered[i].parent < 0) setLevel((int)i, 0);
    }

    regions.swap(ordered);
}

static bool ShouldMergeCoarseBlocks(const RECT& a, const RECT& b, int w, int h) {
    RECT u = UnionRect2(a, b);
    int ua = RectArea(u);
    if (ua <= 0 || ua > (int)(w * h * 0.94)) return false;

    int aw = RectWidth(a), ah = RectHeight(a);
    int bw = RectWidth(b), bh = RectHeight(b);
    int minW = std::max(1, std::min(aw, bw));
    int minH = std::max(1, std::min(ah, bh));
    int gapX = RectGapX(a, b);
    int gapY = RectGapY(a, b);
    int ovY = RangeOverlap(a.top, a.bottom, b.top, b.bottom);
    int ovX = RangeOverlap(a.left, a.right, b.left, b.right);
    double yOverlap = (double)ovY / (double)minH;
    double xOverlap = (double)ovX / (double)minW;

    if (IntersectsInflated(a, b, 8)) return true;

    // Coarse blocks may contain a label + field, icon + label, or several small
    // controls in one row. Keep this pass moderate so separate panels do not
    // collapse into one full-window block.
    if (yOverlap >= 0.42 && gapX <= std::max(18, minH)) return true;
    if (xOverlap >= 0.48 && gapY <= std::max(14, minW / 3)) return true;

    return false;
}

static void MergeCoarseBlocks(std::vector<RECT>& blocks, int w, int h) {
    bool changed = true;
    int round = 0;
    while (changed && round < 10) {
        changed = false;
        ++round;
        for (size_t i = 0; i < blocks.size() && !changed; ++i) {
            for (size_t j = i + 1; j < blocks.size(); ++j) {
                if (ShouldMergeCoarseBlocks(blocks[i], blocks[j], w, h)) {
                    blocks[i] = UnionRect2(blocks[i], blocks[j]);
                    blocks[j] = blocks.back();
                    blocks.pop_back();
                    changed = true;
                    break;
                }
            }
        }
    }
}


static bool IsLargeCoarseBlock(const RECT& rc, int w, int h) {
    int rw = RectWidth(rc);
    int rh = RectHeight(rc);
    int area = RectArea(rc);
    int fullArea = std::max(1, w * h);
    if (area > (int)(fullArea * 0.36)) return true;
    if (rw > w * 72 / 100 && rh > h * 34 / 100) return true;
    if (rw > 900 && rh > 520) return true;
    return false;
}

static int CountRawEdgeInRow(const std::vector<unsigned char>& rawEdge, int w, const RECT& rc, int y) {
    if (y < rc.top || y >= rc.bottom) return 0;
    int h = gEdgeIntegralH > 0 ? gEdgeIntegralH : gImgH;
    int fast = CountEdgeIntegralRect(MakeRect(rc.left, y, rc.right, y + 1), w, h);
    if (fast >= 0) return fast;

    int count = 0;
    for (int x = rc.left; x < rc.right; ++x) {
        if (rawEdge[y * w + x]) ++count;
    }
    return count;
}

static int CountRawEdgeInColumn(const std::vector<unsigned char>& rawEdge, int w, const RECT& rc, int x) {
    if (x < rc.left || x >= rc.right) return 0;
    int h = gEdgeIntegralH > 0 ? gEdgeIntegralH : gImgH;
    int fast = CountEdgeIntegralRect(MakeRect(x, rc.top, x + 1, rc.bottom), w, h);
    if (fast >= 0) return fast;

    int count = 0;
    for (int y = rc.top; y < rc.bottom; ++y) {
        if (rawEdge[y * w + x]) ++count;
    }
    return count;
}

static bool TrySplitBlockHorizontally(const std::vector<unsigned char>& rawEdge, int w, int h, const RECT& block, std::vector<RECT>& parts) {
    RECT rc = InflateRectClamped(block, 0, w, h);
    int rw = RectWidth(rc);
    int rh = RectHeight(rc);
    if (rw < 120 || rh < 150) return false;

    int blankLimit = std::max(1, rw / 260);
    int minBlankRun = ClampInt(rh / 24, 10, 28);
    int minPartH = std::max(36, rh / 12);
    int margin = ClampInt(rh / 40, 6, 18);
    int prev = rc.top;
    int runStart = -1;
    std::vector<RECT> segs;

    for (int y = rc.top + margin; y < rc.bottom - margin; ++y) {
        bool blank = CountRawEdgeInRow(rawEdge, w, rc, y) <= blankLimit;
        if (blank) {
            if (runStart < 0) runStart = y;
        } else if (runStart >= 0) {
            int runEnd = y;
            if (runEnd - runStart >= minBlankRun) {
                if (runStart - prev >= minPartH) {
                    segs.push_back(MakeRect(rc.left, prev, rc.right, runStart));
                }
                prev = runEnd;
            }
            runStart = -1;
        }
    }

    if (runStart >= 0 && rc.bottom - margin - runStart >= minBlankRun) {
        if (runStart - prev >= minPartH) {
            segs.push_back(MakeRect(rc.left, prev, rc.right, runStart));
        }
        prev = rc.bottom - margin;
    }
    if (rc.bottom - prev >= minPartH) {
        segs.push_back(MakeRect(rc.left, prev, rc.right, rc.bottom));
    }

    if (segs.size() < 2) return false;
    int kept = 0;
    for (size_t i = 0; i < segs.size(); ++i) {
        if (RectArea(segs[i]) >= RectArea(rc) / 80) ++kept;
    }
    if (kept < 2) return false;
    parts.swap(segs);
    return true;
}

static bool TrySplitBlockVertically(const std::vector<unsigned char>& rawEdge, int w, int h, const RECT& block, std::vector<RECT>& parts) {
    RECT rc = InflateRectClamped(block, 0, w, h);
    int rw = RectWidth(rc);
    int rh = RectHeight(rc);
    if (rw < 220 || rh < 90) return false;

    int blankLimit = std::max(1, rh / 260);
    int minBlankRun = ClampInt(rw / 24, 12, 34);
    int minPartW = std::max(60, rw / 10);
    int margin = ClampInt(rw / 50, 8, 22);
    int prev = rc.left;
    int runStart = -1;
    std::vector<RECT> segs;

    for (int x = rc.left + margin; x < rc.right - margin; ++x) {
        bool blank = CountRawEdgeInColumn(rawEdge, w, rc, x) <= blankLimit;
        if (blank) {
            if (runStart < 0) runStart = x;
        } else if (runStart >= 0) {
            int runEnd = x;
            if (runEnd - runStart >= minBlankRun) {
                if (runStart - prev >= minPartW) {
                    segs.push_back(MakeRect(prev, rc.top, runStart, rc.bottom));
                }
                prev = runEnd;
            }
            runStart = -1;
        }
    }

    if (runStart >= 0 && rc.right - margin - runStart >= minBlankRun) {
        if (runStart - prev >= minPartW) {
            segs.push_back(MakeRect(prev, rc.top, runStart, rc.bottom));
        }
        prev = rc.right - margin;
    }
    if (rc.right - prev >= minPartW) {
        segs.push_back(MakeRect(prev, rc.top, rc.right, rc.bottom));
    }

    if (segs.size() < 2) return false;
    int kept = 0;
    for (size_t i = 0; i < segs.size(); ++i) {
        if (RectArea(segs[i]) >= RectArea(rc) / 90) ++kept;
    }
    if (kept < 2) return false;
    parts.swap(segs);
    return true;
}

static void SplitCoarseBlockByWhitespace(const std::vector<unsigned char>& rawEdge,
                                         int w, int h,
                                         const RECT& block,
                                         int depth,
                                         std::vector<RECT>& out) {
    RECT rc = InflateRectClamped(block, 0, w, h);
    if (depth >= 2 || !IsLargeCoarseBlock(rc, w, h)) {
        out.push_back(rc);
        return;
    }

    std::vector<RECT> parts;
    if (TrySplitBlockHorizontally(rawEdge, w, h, rc, parts) || TrySplitBlockVertically(rawEdge, w, h, rc, parts)) {
        for (size_t i = 0; i < parts.size(); ++i) {
            RECT part = InflateRectClamped(parts[i], 3, w, h);
            SplitCoarseBlockByWhitespace(rawEdge, w, h, part, depth + 1, out);
        }
        return;
    }

    out.push_back(rc);
}

static void CollectMaskComponentsInArea(const std::vector<unsigned char>& mask,
                                        int w, int h,
                                        const RECT& scanRc,
                                        std::vector<unsigned char>& seen,
                                        int minPixels,
                                        int inflatePad,
                                        int maxArea,
                                        bool coarseMode,
                                        std::vector<RECT>& out) {
    RECT area = InflateRectClamped(scanRc, 0, w, h);
    if (RectWidth(area) <= 0 || RectHeight(area) <= 0) return;

    std::vector<int> stack;
    stack.reserve(4096);
    const int dx[4] = {1,-1,0,0};
    const int dy[4] = {0,0,1,-1};
    for (int y = area.top; y < area.bottom; ++y) {
        for (int x = area.left; x < area.right; ++x) {
            int start = y*w + x;
            if (!mask[start] || seen[start]) continue;

            seen[start] = 1;
            stack.clear();
            stack.push_back(start);
            size_t head = 0;
            int minx = x, maxx = x, miny = y, maxy = y, count = 0;
            while (head < stack.size()) {
                int v = stack[head++];
                int cx = v % w;
                int cy = v / w;
                ++count;
                if (cx < minx) minx = cx; if (cx > maxx) maxx = cx;
                if (cy < miny) miny = cy; if (cy > maxy) maxy = cy;
                for (int k = 0; k < 4; ++k) {
                    int nx = cx + dx[k], ny = cy + dy[k];
                    if (nx < area.left || ny < area.top || nx >= area.right || ny >= area.bottom) continue;
                    int ni = ny*w + nx;
                    if (mask[ni] && !seen[ni]) {
                        seen[ni] = 1;
                        stack.push_back(ni);
                    }
                }
            }

            if (count < minPixels) continue;
            RECT rc = InflateRectClamped(MakeRect(minx, miny, maxx + 1, maxy + 1), inflatePad, w, h);
            int rw = RectWidth(rc);
            int rh = RectHeight(rc);
            int areaPx = RectArea(rc);
            if (rw <= 0 || rh <= 0 || areaPx <= 0 || areaPx > maxArea) continue;

            if (coarseMode) {
                if (rw < 22 || rh < 14) continue;
                if (areaPx < 280) continue;
                if ((rw <= 6 && rh > 80) || (rh <= 6 && rw > 80)) continue;
                out.push_back(rc);
            } else {
                if (rw < 6 || rh < 6) continue;
                if (areaPx < 48) continue;
                if (rw > w * 0.985 && rh > h * 0.985) continue;
                if ((rw <= 4 && rh > 40) || (rh <= 4 && rw > 40)) continue;
                out.push_back(rc);
            }
        }
    }
}

static std::vector<RECT> BuildCoarseSearchBlocks(const std::vector<unsigned char>& rawEdge, int w, int h) {
    std::vector<RECT> blocks;
    if (w <= 0 || h <= 0 || rawEdge.empty()) return blocks;

    std::vector<unsigned char> coarse = rawEdge;

    // Stage 1: group strokes into structural blocks. The radius is much larger
    // than the fine search radius, so fragmented text and nearby controls become
    // one search island, but separate panels keep a whitespace gap between them.
    int rx = ClampInt(w / 90, 10, 24);
    int ry = ClampInt(h / 120, 5, 12);
    Dilate(coarse, w, h, rx, ry, 1);
    Dilate(coarse, w, h, std::max(4, rx / 2), std::max(3, ry / 2), 1);

    std::vector<unsigned char> seen((size_t)w * h, 0);
    RECT full = MakeRect(0, 0, w, h);
    int maxArea = std::max(1, w * h);
    CollectMaskComponentsInArea(coarse, w, h, full, seen, 20, 8, maxArea, true, blocks);

    MergeCoarseBlocks(blocks, w, h);

    // Very large coarse islands are split again by real whitespace bands. This
    // keeps a dense page or settings window from becoming one global search area.
    std::vector<RECT> refinedBlocks;
    for (size_t i = 0; i < blocks.size(); ++i) {
        SplitCoarseBlockByWhitespace(rawEdge, w, h, blocks[i], 0, refinedBlocks);
    }
    blocks.swap(refinedBlocks);
    RemoveNearDuplicateRects(blocks);

    // If the coarse pass found too many tiny islands, keep the most useful ones.
    // This avoids quadratic fine merging on noisy images.
    if (blocks.size() > kMaxCoarseBlocks) {
        std::sort(blocks.begin(), blocks.end(), [](const RECT& a, const RECT& b){
            return RectArea(a) > RectArea(b);
        });
        blocks.resize(kMaxCoarseBlocks);
    }

    if (blocks.empty()) {
        blocks.push_back(full);
    }

    SortRectsReadingOrder(blocks);
    return blocks;
}


static std::vector<RECT> BuildLargeSceneRanges(const std::vector<unsigned char>& rawEdge,
                                               int w, int h,
                                               const std::vector<RECT>& coarseBlocks) {
    std::vector<RECT> scenes;
    if (w <= 0 || h <= 0 || rawEdge.empty()) return scenes;

    RECT full = MakeRect(0, 0, w, h);
    scenes.push_back(full);

    // Large-range scene recognition: use a much stronger dilation than normal
    // block detection, so one toolbar/sidebar/content panel becomes one scene.
    std::vector<unsigned char> macro = rawEdge;
    int rx = ClampInt(w / 34, 18, 54);
    int ry = ClampInt(h / 36, 12, 36);
    Dilate(macro, w, h, rx, ry, 1);
    Dilate(macro, w, h, std::max(8, rx / 2), std::max(6, ry / 2), 1);

    std::vector<unsigned char> seen((size_t)w * h, 0);
    std::vector<RECT> macroBlocks;
    CollectMaskComponentsInArea(macro, w, h, full, seen, 35, 10, std::max(1, w * h), true, macroBlocks);
    MergeCoarseBlocks(macroBlocks, w, h);

    // Also infer scenes from the range blocks: aligned ranges often describe a
    // sidebar, toolbar, list, form area or content area even when edge density is low.
    std::vector<RECT> inferred = macroBlocks;
    for (size_t i = 0; i < coarseBlocks.size(); ++i) {
        RECT base = InflateRectClamped(coarseBlocks[i], 6, w, h);
        int baseArea = RectArea(base);
        if (baseArea < 700) continue;
        RECT cluster = base;
        int count = 1;
        for (size_t j = 0; j < coarseBlocks.size(); ++j) {
            if (i == j) continue;
            RECT r = InflateRectClamped(coarseBlocks[j], 6, w, h);
            int minH = std::max(1, std::min(RectHeight(cluster), RectHeight(r)));
            int minW = std::max(1, std::min(RectWidth(cluster), RectWidth(r)));
            int ovY = RangeOverlap(cluster.top, cluster.bottom, r.top, r.bottom);
            int ovX = RangeOverlap(cluster.left, cluster.right, r.left, r.right);
            int gapX = RectGapX(cluster, r);
            int gapY = RectGapY(cluster, r);
            bool sameBand = ovY >= minH * 36 / 100 && gapX <= std::max(30, minH * 2);
            bool sameColumn = ovX >= minW * 42 / 100 && gapY <= std::max(26, minW / 2);
            if (sameBand || sameColumn) {
                cluster = UnionRect2(cluster, r);
                ++count;
            }
        }
        int ca = RectArea(cluster);
        int sceneFullLimit = std::max(1, (int)((long long)w * h * 92 / 100));
        if (count >= 2 && ca >= 1600 && ca < sceneFullLimit) inferred.push_back(cluster);
    }

    RemoveNearDuplicateRects(inferred);
    for (size_t i = 0; i < inferred.size(); ++i) {
        std::vector<RECT> parts;
        SplitCoarseBlockByWhitespace(rawEdge, w, h, inferred[i], 0, parts);
        if (parts.empty()) parts.push_back(inferred[i]);
        for (size_t k = 0; k < parts.size(); ++k) {
            RECT r = InflateRectClamped(parts[k], 2, w, h);
            int area = RectArea(r);
            if (area < 1400) continue;
            if (RectIoU(r, full) >= 0.86) continue;
            bool dup = false;
            for (size_t s = 0; s < scenes.size(); ++s) {
                int sa = RectArea(scenes[s]);
                int small = std::min(area, sa);
                int large = std::max(area, sa);
                RECT in = IntersectRect2(r, scenes[s]);
                int ia = RectArea(in);
                if (RectIoU(r, scenes[s]) >= 0.70 || (small > 0 && ia >= small * 88 / 100 && large <= small * 13 / 10)) {
                    dup = true;
                    break;
                }
            }
            if (!dup) scenes.push_back(r);
        }
    }

    if (scenes.size() > kMaxSceneRanges) {
        std::sort(scenes.begin() + 1, scenes.end(), [](const RECT& a, const RECT& b) {
            return RectArea(a) > RectArea(b);
        });
        scenes.resize(kMaxSceneRanges);
    }
    SortRectsReadingOrder(scenes);
    return scenes;
}

static void MergeLocalRegions(std::vector<RECT>& rects, int maxArea) {
    bool changed = true;
    int mergeRound = 0;
    while (changed && mergeRound < 12) {
        changed = false;
        ++mergeRound;
        for (size_t i = 0; i < rects.size() && !changed; ++i) {
            for (size_t j = i + 1; j < rects.size(); ++j) {
                if (ShouldMergeRegions(rects[i], rects[j], maxArea)) {
                    rects[i] = UnionRect2(rects[i], rects[j]);
                    rects[j] = rects.back();
                    rects.pop_back();
                    changed = true;
                    break;
                }
            }
        }
    }
}

static std::vector<unsigned char> BuildGrayImageFromCapture(int w, int h) {
    std::vector<unsigned char> gray((size_t)w * h);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            size_t p = ((size_t)y * w + x) * 4;
            int b = gPixels[p + 0];
            int g = gPixels[p + 1];
            int r = gPixels[p + 2];
            gray[(size_t)y * w + x] = (unsigned char)((r * 30 + g * 59 + b * 11) / 100);
        }
    }
    return gray;
}

static std::vector<unsigned char> BuildRawEdgeMask(const std::vector<unsigned char>& gray, int w, int h) {
    std::vector<unsigned short> edgeMag((size_t)w * h, 0);
    double sum = 0.0;
    double sum2 = 0.0;
    int samples = 0;
    int sampleStep = (w * h > 2500000) ? 2 : 1;

    for (int y = 1; y < h - 1; ++y) {
        for (int x = 1; x < w - 1; ++x) {
            int mag = EdgeStrengthAt(gray, w, h, x, y);
            if (mag > 65535) mag = 65535;
            edgeMag[(size_t)y * w + x] = (unsigned short)mag;
            if (((y - 1) % sampleStep) == 0 && ((x - 1) % sampleStep) == 0) {
                sum += mag;
                sum2 += (double)mag * mag;
                ++samples;
            }
        }
    }

    double mean = samples > 0 ? sum / samples : 0.0;
    double var = samples > 0 ? (sum2 / samples - mean * mean) : 0.0;
    if (var < 0.0) var = 0.0;
    int threshold = ClampInt((int)(mean + std::sqrt(var) * 1.08), 42, 165);
    int softThreshold = ClampInt((threshold * 72) / 100, 28, threshold);

    std::vector<unsigned char> rawEdge((size_t)w * h, 0);
    for (int y = 1; y < h - 1; ++y) {
        for (int x = 1; x < w - 1; ++x) {
            int mag = edgeMag[(size_t)y * w + x];
            if (mag >= threshold || (mag >= softThreshold && LocalContrastAt(gray, w, h, x, y) >= 22)) {
                rawEdge[(size_t)y * w + x] = 1;
            }
        }
    }
    BuildEdgeIntegral(rawEdge, w, h);
    return rawEdge;
}

static std::vector<RECT> CollectFineRectsInBlocks(const std::vector<unsigned char>& rawEdge,
                                                   const std::vector<RECT>& coarseBlocks,
                                                   int w, int h) {
    std::vector<unsigned char> edge = rawEdge;
    Dilate(edge, w, h, 2, 1, 1);

    std::vector<unsigned char> fineSeen((size_t)w * h, 0);
    std::vector<RECT> rects;
    int globalMaxArea = std::max(1, (int)((long long)w * h * 92 / 100));

    for (size_t bi = 0; bi < coarseBlocks.size(); ++bi) {
        RECT block = InflateRectClamped(coarseBlocks[bi], 1, w, h);
        int blockArea = std::max(1, RectArea(block));
        int localMaxArea = std::min(globalMaxArea, std::max(240, (int)(blockArea * 0.88)));
        std::vector<RECT> localRects;

        CollectMaskComponentsInArea(edge, w, h, block, fineSeen, 8, 2, localMaxArea, false, localRects);
        if (localRects.empty()) continue;

        MergeLocalRegions(localRects, localMaxArea);
        MergeTextFragments(localRects, rawEdge, w, h);
        rects.insert(rects.end(), localRects.begin(), localRects.end());
    }
    RemoveNearDuplicateRects(rects);
    return rects;
}

static void PushRegion(std::vector<Region>& candidates, const RECT& rc, double confidence, int kind) {
    Region reg;
    reg.rc = rc;
    reg.confidence = ClampDouble(confidence, 0.10, 0.98);
    reg.kind = kind;
    reg.parent = -1;
    reg.level = 0;
    reg.childCount = 0;
    candidates.push_back(reg);
}

static bool LooksLikeExistingScene(const RECT& r, const std::vector<RECT>& sceneRanges) {
    int area = RectArea(r);
    for (size_t s = 0; s < sceneRanges.size(); ++s) {
        int sceneArea = RectArea(sceneRanges[s]);
        if (area <= 0 || sceneArea <= 0) continue;
        int small = std::min(area, sceneArea);
        int large = std::max(area, sceneArea);
        if (RectIoU(r, sceneRanges[s]) >= 0.90 && large <= small * 11 / 10) return true;
    }
    return false;
}

static bool LooksLikeExistingCoarseBlock(const RECT& r, const std::vector<RECT>& coarseBlocks) {
    int rectArea = RectArea(r);
    for (size_t b = 0; b < coarseBlocks.size(); ++b) {
        int blockArea = RectArea(coarseBlocks[b]);
        if (rectArea <= 0 || blockArea <= 0) continue;
        int small = std::min(rectArea, blockArea);
        int large = std::max(rectArea, blockArea);
        if (RectIoU(r, coarseBlocks[b]) >= 0.86 && large <= small * 13 / 10) return true;
    }
    return false;
}

static void AppendSceneAndRangeNodes(std::vector<Region>& candidates,
                                      const std::vector<unsigned char>& rawEdge,
                                      const std::vector<RECT>& sceneRanges,
                                      const std::vector<RECT>& coarseBlocks,
                                      int w, int h) {
    for (size_t i = 0; i < sceneRanges.size(); ++i) {
        RECT r = InflateRectClamped(sceneRanges[i], 0, w, h);
        if (RectArea(r) < 1200) continue;
        if (RegionRectDuplicate(candidates, r, REGION_KIND_SCENE)) continue;
        PushRegion(candidates, r, ScoreRegion(rawEdge, w, h, r) * 0.72 + 0.20, REGION_KIND_SCENE);
    }

    for (size_t i = 0; i < coarseBlocks.size(); ++i) {
        RECT r = InflateRectClamped(coarseBlocks[i], 0, w, h);
        if (RectArea(r) < 360) continue;
        if (LooksLikeExistingScene(r, sceneRanges)) continue;
        PushRegion(candidates, r, ScoreRegion(rawEdge, w, h, r) * 0.90 + 0.06, REGION_KIND_BLOCK);
    }
}

static void AppendTextNodes(std::vector<Region>& candidates,
                            const std::vector<unsigned char>& rawEdge,
                            const std::vector<TextCluster>& textClusters,
                            int w, int h) {
    for (size_t i = 0; i < textClusters.size(); ++i) {
        RECT block = InflateRectClamped(textClusters[i].block, 0, w, h);
        if (RectArea(block) < 80) continue;
        if (!RegionRectDuplicate(candidates, block, REGION_KIND_TEXT_BLOCK)) {
            PushRegion(candidates, block, ScoreRegion(rawEdge, w, h, block) * 0.86 + 0.12, REGION_KIND_TEXT_BLOCK);
        }

        if (textClusters[i].lines.size() >= 2) {
            for (size_t k = 0; k < textClusters[i].lines.size(); ++k) {
                RECT line = InflateRectClamped(textClusters[i].lines[k], 1, w, h);
                if (RectArea(line) < 60) continue;
                if (RectIoU(line, block) >= 0.88) continue;
                if (RegionRectDuplicate(candidates, line, REGION_KIND_TEXT)) continue;
                PushRegion(candidates, line, ScoreRegion(rawEdge, w, h, line) * 0.92 + 0.08, REGION_KIND_TEXT);
            }
        }
    }
}

static void AppendElementNodes(std::vector<Region>& candidates,
                               const std::vector<unsigned char>& rawEdge,
                               const std::vector<RECT>& rects,
                               const std::vector<RECT>& coarseBlocks,
                               const std::vector<TextCluster>& textClusters,
                               int w, int h) {
    for (size_t i = 0; i < rects.size(); ++i) {
        if (LooksLikeExistingCoarseBlock(rects[i], coarseBlocks)) continue;
        if (IsTextRectCoveredByCluster(rects[i], textClusters, rawEdge, w, h)) continue;

        int kind = ClassifyElementRegion(rawEdge, w, h, rects[i]);
        double confidence = ScoreRegion(rawEdge, w, h, rects[i]);
        if (kind == REGION_KIND_IMAGE) confidence = ClampDouble(confidence + 0.10, 0.10, 0.98);
        if (kind == REGION_KIND_ICON) confidence = ClampDouble(confidence + 0.06, 0.10, 0.98);
        PushRegion(candidates, rects[i], confidence, kind);
    }
}

static void FinalLimitVisualDomNodes(std::vector<Region>& regions) {
    if (regions.size() <= kMaxVisualDomNodes) return;

    std::sort(regions.begin(), regions.end(), [](const Region& a, const Region& b) {
        if (a.kind != b.kind) return RegionKindPriority(a.kind) > RegionKindPriority(b.kind);
        if (a.childCount != b.childCount) return a.childCount > b.childCount;
        if (std::abs(a.confidence - b.confidence) > 0.0001) return a.confidence > b.confidence;
        return RectArea(a.rc) > RectArea(b.rc);
    });
    regions.resize(kMaxVisualDomNodes);
}

static std::vector<Region> AnalyzeImageRegions() {
    std::vector<Region> out;
    if (gImgW <= 0 || gImgH <= 0 || gPixels.empty()) return out;

    int w = gImgW;
    int h = gImgH;

    // Pipeline: cache low-level pixels once, then let later stages reuse the mask,
    // integral image and local candidate lists. This keeps the visual DOM result but
    // avoids repeated full-window scans.
    std::vector<unsigned char> gray = BuildGrayImageFromCapture(w, h);
    std::vector<unsigned char> rawEdge = BuildRawEdgeMask(gray, w, h);
    gray.clear();
    gray.shrink_to_fit();

    std::vector<RECT> coarseBlocks = BuildCoarseSearchBlocks(rawEdge, w, h);
    std::vector<RECT> sceneRanges = BuildLargeSceneRanges(rawEdge, w, h, coarseBlocks);
    std::vector<RECT> rects = CollectFineRectsInBlocks(rawEdge, coarseBlocks, w, h);

    std::vector<TextCluster> textClusters = BuildTextClusters(rects, rawEdge, w, h);
    SuppressTextPiecesCoveredByClusters(rects, textClusters, rawEdge, w, h);
    RemoveNearDuplicateRects(rects);

    std::vector<Region> candidates;
    candidates.reserve(sceneRanges.size() + coarseBlocks.size() + textClusters.size() * 3 + rects.size() + 32);

    AppendSceneAndRangeNodes(candidates, rawEdge, sceneRanges, coarseBlocks, w, h);
    AppendTextNodes(candidates, rawEdge, textClusters, w, h);
    AppendElementNodes(candidates, rawEdge, rects, coarseBlocks, textClusters, w, h);
    AddSyntheticLayoutGroups(candidates, rawEdge, w, h);

    std::sort(candidates.begin(), candidates.end(), [](const Region& a, const Region& b){
        if (a.kind != b.kind) return RegionKindPriority(a.kind) > RegionKindPriority(b.kind);
        if (std::abs(a.confidence - b.confidence) > 0.0001) return a.confidence > b.confidence;
        return RectArea(a.rc) < RectArea(b.rc);
    });
    if (candidates.size() > kMaxVisualDomNodes) candidates.resize(kMaxVisualDomNodes);

    BuildRegionTree(candidates);
    if (PruneWeakGroupNodes(candidates)) BuildRegionTree(candidates);
    if (PruneOverDetailedVisualDomNodes(candidates, textClusters, rawEdge, w, h)) {
        BuildRegionTree(candidates);
        if (PruneWeakGroupNodes(candidates)) BuildRegionTree(candidates);
    }
    FinalLimitVisualDomNodes(candidates);
    BuildRegionTree(candidates);

    out.swap(candidates);
    return out;
}

static HTREEITEM FindSelectedTreeItem() {
    return TreeView_GetSelection(gTree);
}

static NodeData* GetNodeData(HTREEITEM item) {
    if (!item) return NULL;
    TVITEMW tv;
    ZeroMemory(&tv, sizeof(tv));
    tv.mask = TVIF_PARAM;
    tv.hItem = item;
    if (!TreeView_GetItem(gTree, &tv)) return NULL;
    return (NodeData*)tv.lParam;
}

static HTREEITEM FindWindowItemFromSelection(HTREEITEM item) {
    while (item) {
        NodeData* d = GetNodeData(item);
        if (d && d->type == 0) return item;
        item = TreeView_GetParent(gTree, item);
    }
    return NULL;
}

static void DeleteOldAnalysisChildren(HTREEITEM winItem) {
    HTREEITEM child = TreeView_GetChild(gTree, winItem);
    while (child) {
        HTREEITEM next = TreeView_GetNextSibling(gTree, child);
        NodeData* d = GetNodeData(child);
        if (d && d->type == 1) TreeView_DeleteItem(gTree, child);
        child = next;
    }
}

static std::wstring MakeRegionNodeText(int index) {
    const Region& reg = gRegions[index];
    RECT r = reg.rc;
    RECT sr = MakeRect(gCaptureScreenRect.left + r.left, gCaptureScreenRect.top + r.top,
                       gCaptureScreenRect.left + r.right, gCaptureScreenRect.top + r.bottom);
    wchar_t buf[420];
    int conf = (int)(reg.confidence * 100.0 + 0.5);
    if (reg.childCount > 0) {
        wsprintfW(buf, L"%s %03d  子项=%d  可信度=%d%%  图像=(%d,%d,%d,%d)  屏幕=(%d,%d,%d,%d)",
                  RegionKindName(reg.kind), index + 1, reg.childCount, conf,
                  r.left, r.top, RectWidth(r), RectHeight(r),
                  sr.left, sr.top, RectWidth(sr), RectHeight(sr));
    } else {
        wsprintfW(buf, L"%s %03d  可信度=%d%%  图像=(%d,%d,%d,%d)  屏幕=(%d,%d,%d,%d)",
                  RegionKindName(reg.kind), index + 1, conf,
                  r.left, r.top, RectWidth(r), RectHeight(r),
                  sr.left, sr.top, RectWidth(sr), RectHeight(sr));
    }
    return std::wstring(buf);
}

static void InsertRegionTreeNode(HTREEITEM parent, int index) {
    if (index < 0 || index >= (int)gRegions.size()) return;
    HTREEITEM item = TreeInsert(parent, MakeRegionNodeText(index), NewNode(2, gSelectedHwnd, index));
    if (index < (int)gRegionChildren.size()) {
        for (size_t i = 0; i < gRegionChildren[index].size(); ++i) {
            InsertRegionTreeNode(item, gRegionChildren[index][i]);
        }
    }
    if (gRegions[index].level <= 1 && gRegions[index].childCount > 0) {
        TreeView_Expand(gTree, item, TVE_EXPAND);
    }
}

static void AddAnalysisToTree(HTREEITEM winItem) {
    DeleteOldAnalysisChildren(winItem);
    int rootCount = 0;
    for (size_t i = 0; i < gRegions.size(); ++i) {
        if (gRegions[i].parent < 0) ++rootCount;
    }
    wchar_t rootText[160];
    wsprintfW(rootText, L"视觉场景树（%d 个节点，%d 个根场景）", (int)gRegions.size(), rootCount);
    HTREEITEM root = TreeInsert(winItem, rootText, NewNode(1, gSelectedHwnd, -1));
    for (size_t i = 0; i < gRegions.size(); ++i) {
        if (gRegions[i].parent < 0) InsertRegionTreeNode(root, (int)i);
    }
    TreeView_Expand(gTree, winItem, TVE_EXPAND);
    TreeView_Expand(gTree, root, TVE_EXPAND);
}

static void AnalyzeSelectedWindow() {
    HTREEITEM sel = FindSelectedTreeItem();
    HTREEITEM winItem = FindWindowItemFromSelection(sel);
    if (!winItem) {
        MessageBoxW(gMain, L"请先在左侧树中选择一个窗口节点。", L"杰睿视觉分析工具", MB_ICONINFORMATION);
        return;
    }
    NodeData* wd = GetNodeData(winItem);
    if (!wd || !IsWindow(wd->hwnd)) {
        MessageBoxW(gMain, L"选中的窗口已不可用，请刷新窗口列表。", L"杰睿视觉分析工具", MB_ICONWARNING);
        return;
    }
    gSelectedHwnd = wd->hwnd;
    gSelectedRegion = -1;

    if (gSelectedHwnd == gMain || IsChild(gMain, gSelectedHwnd)) {
        MessageBoxW(gMain, L"请选择杰睿视觉分析工具之外的目标窗口。", L"杰睿视觉分析工具", MB_ICONINFORMATION);
        return;
    }

    // Hide this tool and its screen overlay before capturing the target window.
    // This prevents the analyzer window or previous annotation rectangles from being captured.
    ShowWindow(gOverlay, SW_HIDE);
    ShowWindow(gMain, SW_HIDE);
    Sleep(160);

    HDC screenDc = GetDC(NULL);
    if (screenDc) {
        RedrawWindow(NULL, NULL, NULL, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
        GdiFlush();
        ReleaseDC(NULL, screenDc);
    }
    Sleep(80);

    bool captured = CaptureWindowImage(gSelectedHwnd);

    ShowWindow(gMain, SW_SHOW);
    SetForegroundWindow(gMain);

    if (!captured) {
        MessageBoxW(gMain, L"无法截图选中的窗口。", L"杰睿视觉分析工具", MB_ICONWARNING);
        RefreshOverlay();
        return;
    }

    DWORD analyzeStart = GetTickCount();
    gRegions = AnalyzeImageRegions();
    DWORD analyzeMs = GetTickCount() - analyzeStart;
    AddAnalysisToTree(winItem);
    wchar_t st[360];
    wsprintfW(st, L"分析完成。共识别到 %d 个视觉区域，用时 %lu ms。截图尺寸：%dx%d，屏幕位置：(%d,%d)。",
              (int)gRegions.size(), (unsigned long)analyzeMs, gImgW, gImgH, gCaptureScreenRect.left, gCaptureScreenRect.top);
    SetStatusText(st);
    InvalidateRect(gMain, NULL, TRUE);
    RefreshOverlay();
}

static void Layout(HWND hwnd) {
    RECT rc;
    GetClientRect(hwnd, &rc);
    int top = 8;
    int hbar = 30;
    int statusH = 24;
    MoveWindow(gBtnRefresh, 8, top, 120, hbar, TRUE);
    MoveWindow(gBtnAnalyze, 136, top, 130, hbar, TRUE);
    MoveWindow(gChkAnnotate, 276, top+4, 160, 24, TRUE);
    MoveWindow(gTree, 8, top + hbar + 8, gLeftWidth, rc.bottom - statusH - (top + hbar + 16), TRUE);
    MoveWindow(gStatus, 8, rc.bottom - statusH, rc.right - 16, statusH, TRUE);
    gPreviewRect.left = gLeftWidth + 18;
    gPreviewRect.top = top + hbar + 8;
    gPreviewRect.right = rc.right - 8;
    gPreviewRect.bottom = rc.bottom - statusH - 8;
}

static void DrawPreview(HDC hdc) {
    RECT area = gPreviewRect;
    HBRUSH bg = CreateSolidBrush(RGB(248,248,248));
    FillRect(hdc, &area, bg);
    DeleteObject(bg);
    HPEN border = CreatePen(PS_SOLID, 1, RGB(210,210,210));
    HGDIOBJ oldPen = SelectObject(hdc, border);
    SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Rectangle(hdc, area.left, area.top, area.right, area.bottom);
    SelectObject(hdc, oldPen);
    DeleteObject(border);

    if (gImgW <= 0 || gImgH <= 0 || gPixels.empty()) {
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(100,100,100));
        std::wstring msg = L"请先选择窗口，然后点击“分析”。截图预览和视觉区域标注会显示在这里。";
        RECT tr = area;
        tr.left += 20; tr.top += 20;
        DrawTextW(hdc, msg.c_str(), -1, &tr, DT_LEFT | DT_TOP | DT_WORDBREAK);
        return;
    }

    int aw = area.right - area.left - 20;
    int ah = area.bottom - area.top - 20;
    double sx = (double)aw / gImgW;
    double sy = (double)ah / gImgH;
    gPreviewScale = std::min(sx, sy);
    if (gPreviewScale <= 0) gPreviewScale = 1.0;
    int dw = (int)(gImgW * gPreviewScale);
    int dh = (int)(gImgH * gPreviewScale);
    gPreviewX = area.left + 10 + (aw - dw) / 2;
    gPreviewY = area.top + 10 + (ah - dh) / 2;

    BITMAPINFO bi;
    ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = gImgW;
    bi.bmiHeader.biHeight = -gImgH;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    SetStretchBltMode(hdc, HALFTONE);
    StretchDIBits(hdc, gPreviewX, gPreviewY, dw, dh, 0, 0, gImgW, gImgH, gPixels.data(), &bi, DIB_RGB_COLORS, SRCCOPY);

    HFONT oldFont = (HFONT)SelectObject(hdc, GetStockObject(DEFAULT_GUI_FONT));
    SetBkMode(hdc, OPAQUE);
    for (size_t i = 0; i < gRegions.size(); ++i) {
        RECT r = gRegions[i].rc;
        int x1 = gPreviewX + (int)(r.left * gPreviewScale);
        int y1 = gPreviewY + (int)(r.top * gPreviewScale);
        int x2 = gPreviewX + (int)(r.right * gPreviewScale);
        int y2 = gPreviewY + (int)(r.bottom * gPreviewScale);
        bool hot = ((int)i == gSelectedRegion);
        HPEN pen = CreatePen(hot ? PS_SOLID : PS_DOT, hot ? 3 : 1, hot ? RGB(255,0,0) : RGB(230,80,40));
        HGDIOBJ op = SelectObject(hdc, pen);
        SelectObject(hdc, GetStockObject(NULL_BRUSH));
        Rectangle(hdc, x1, y1, x2, y2);
        SelectObject(hdc, op);
        DeleteObject(pen);
        wchar_t num[32];
        wsprintfW(num, L"%d", (int)i + 1);
        RECT nr = {x1, y1, x1 + 46, y1 + 18};
        SetTextColor(hdc, RGB(255,255,255));
        SetBkColor(hdc, hot ? RGB(255,0,0) : RGB(230,80,40));
        DrawTextW(hdc, num, -1, &nr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }
    SelectObject(hdc, oldFont);
}

static void DrawOverlay(HDC hdc) {
    RECT vr;
    vr.left = GetSystemMetrics(SM_XVIRTUALSCREEN);
    vr.top = GetSystemMetrics(SM_YVIRTUALSCREEN);
    vr.right = vr.left + GetSystemMetrics(SM_CXVIRTUALSCREEN);
    vr.bottom = vr.top + GetSystemMetrics(SM_CYVIRTUALSCREEN);
    HBRUSH clearBrush = CreateSolidBrush(RGB(0,0,0));
    FillRect(hdc, &vr, clearBrush);
    DeleteObject(clearBrush);

    if (!gScreenAnnotate || gRegions.empty() || RectWidth(gCaptureScreenRect) <= 0 || RectHeight(gCaptureScreenRect) <= 0) return;

    SetBkMode(hdc, OPAQUE);
    HFONT oldFont = (HFONT)SelectObject(hdc, GetStockObject(DEFAULT_GUI_FONT));
    for (size_t i = 0; i < gRegions.size(); ++i) {
        if (gSelectedRegion >= 0 && (int)i != gSelectedRegion) continue;
        RECT r = gRegions[i].rc;
        int x1 = gCaptureScreenRect.left + r.left;
        int y1 = gCaptureScreenRect.top + r.top;
        int x2 = gCaptureScreenRect.left + r.right;
        int y2 = gCaptureScreenRect.top + r.bottom;
        bool hot = ((int)i == gSelectedRegion || gSelectedRegion < 0);
        HPEN pen = CreatePen(PS_SOLID, hot ? 3 : 2, hot ? RGB(255,0,0) : RGB(255,128,0));
        HGDIOBJ op = SelectObject(hdc, pen);
        SelectObject(hdc, GetStockObject(NULL_BRUSH));
        Rectangle(hdc, x1, y1, x2, y2);
        SelectObject(hdc, op);
        DeleteObject(pen);

        wchar_t num[32];
        wsprintfW(num, L"%d", (int)i + 1);
        RECT nr = {x1, y1, x1 + 48, y1 + 20};
        SetTextColor(hdc, RGB(255,255,255));
        SetBkColor(hdc, hot ? RGB(255,0,0) : RGB(255,128,0));
        DrawTextW(hdc, num, -1, &nr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }
    SelectObject(hdc, oldFont);
}

static void RefreshOverlay() {
    if (!gOverlay) return;
    RECT vr;
    vr.left = GetSystemMetrics(SM_XVIRTUALSCREEN);
    vr.top = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    SetWindowPos(gOverlay, HWND_TOPMOST, vr.left, vr.top, vw, vh, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    BOOL show = gScreenAnnotate && !gRegions.empty() && RectWidth(gCaptureScreenRect) > 0 && RectHeight(gCaptureScreenRect) > 0;
    ShowWindow(gOverlay, show ? SW_SHOWNOACTIVATE : SW_HIDE);
    InvalidateRect(gOverlay, NULL, TRUE);
    UpdateWindow(gOverlay);
}

static LRESULT CALLBACK OverlayProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            DrawOverlay(hdc);
            EndPaint(hwnd, &ps);
        }
        return 0;
    case WM_NCHITTEST:
        return HTTRANSPARENT;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void CreateOverlayWindow() {
    WNDCLASSW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.lpfnWndProc = OverlayProc;
    wc.hInstance = gInst;
    wc.lpszClassName = L"JeriVisionOverlayWindow";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    RegisterClassW(&wc);

    int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    gOverlay = CreateWindowExW(WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW,
        wc.lpszClassName, L"JeriVisionOverlay", WS_POPUP,
        vx, vy, vw, vh, NULL, NULL, gInst, NULL);
    if (gOverlay) {
        SetLayeredWindowAttributes(gOverlay, RGB(0,0,0), 255, LWA_COLORKEY);
        ShowWindow(gOverlay, SW_HIDE);
    }
}

static LRESULT OnNotify(LPARAM lParam) {
    LPNMHDR hdr = (LPNMHDR)lParam;
    if (hdr->idFrom == IDC_TREE && hdr->code == TVN_SELCHANGEDW) {
        NMTREEVIEWW* tv = (NMTREEVIEWW*)lParam;
        NodeData* d = (NodeData*)tv->itemNew.lParam;
        if (d) {
            if (d->type == 0) {
                gSelectedHwnd = d->hwnd;
                gSelectedRegion = -1;
                SetStatusText(GetWindowTextSafe(d->hwnd));
            } else if (d->type == 1) {
                gSelectedHwnd = d->hwnd;
                gSelectedRegion = -1;
                wchar_t st[160];
                wsprintfW(st, L"视觉分析树根节点，共 %d 个节点。", (int)gRegions.size());
                SetStatusText(st);
            } else if (d->type == 2) {
                gSelectedHwnd = d->hwnd;
                gSelectedRegion = d->regionIndex;
                if (gSelectedRegion >= 0 && gSelectedRegion < (int)gRegions.size()) {
                    RECT r = gRegions[gSelectedRegion].rc;
                    RECT sr = MakeRect(gCaptureScreenRect.left + r.left, gCaptureScreenRect.top + r.top,
                                       gCaptureScreenRect.left + r.right, gCaptureScreenRect.top + r.bottom);
                    const Region& reg = gRegions[gSelectedRegion];
                    wchar_t st[360];
                    wsprintfW(st, L"已选中%s %d：层级=%d 子项=%d，图像 x=%d y=%d 宽=%d 高=%d，屏幕 x=%d y=%d 宽=%d 高=%d",
                              RegionKindName(reg.kind), gSelectedRegion + 1, reg.level, reg.childCount,
                              r.left, r.top, RectWidth(r), RectHeight(r),
                              sr.left, sr.top, RectWidth(sr), RectHeight(sr));
                    SetStatusText(st);
                }
            }
            InvalidateRect(gMain, &gPreviewRect, TRUE);
            RefreshOverlay();
        }
    }
    return 0;
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:
        gMain = hwnd;
        InitCommonControls();
        CreateOverlayWindow();
        gBtnRefresh = CreateWindowW(L"BUTTON", L"刷新窗口", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0,0,0,0, hwnd, (HMENU)IDC_REFRESH, gInst, NULL);
        gBtnAnalyze = CreateWindowW(L"BUTTON", L"分析", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0,0,0,0, hwnd, (HMENU)IDC_ANALYZE, gInst, NULL);
        gChkAnnotate = CreateWindowW(L"BUTTON", L"屏幕标注", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 0,0,0,0, hwnd, (HMENU)IDC_ANNOTATE, gInst, NULL);
        SendMessageW(gChkAnnotate, BM_SETCHECK, BST_CHECKED, 0);
        gTree = CreateWindowExW(WS_EX_CLIENTEDGE, WC_TREEVIEWW, L"", WS_CHILD | WS_VISIBLE | TVS_HASLINES | TVS_LINESATROOT | TVS_HASBUTTONS | TVS_SHOWSELALWAYS, 0,0,0,0, hwnd, (HMENU)IDC_TREE, gInst, NULL);
        gStatus = CreateWindowW(L"STATIC", L"就绪。", WS_CHILD | WS_VISIBLE | SS_LEFT, 0,0,0,0, hwnd, (HMENU)IDC_STATUS, gInst, NULL);
        {
            HFONT guiFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
            if (guiFont) {
                SendMessageW(gBtnRefresh, WM_SETFONT, (WPARAM)guiFont, TRUE);
                SendMessageW(gBtnAnalyze, WM_SETFONT, (WPARAM)guiFont, TRUE);
                SendMessageW(gChkAnnotate, WM_SETFONT, (WPARAM)guiFont, TRUE);
                SendMessageW(gTree, WM_SETFONT, (WPARAM)guiFont, TRUE);
                SendMessageW(gStatus, WM_SETFONT, (WPARAM)guiFont, TRUE);
            }
        }
        RefreshWindows();
        return 0;
    case WM_SIZE:
        Layout(hwnd);
        InvalidateRect(hwnd, NULL, TRUE);
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_REFRESH:
            RefreshWindows();
            return 0;
        case IDC_ANALYZE:
            AnalyzeSelectedWindow();
            return 0;
        case IDC_ANNOTATE:
            gScreenAnnotate = (SendMessageW(gChkAnnotate, BM_GETCHECK, 0, 0) == BST_CHECKED);
            RedrawTextControl(gChkAnnotate);
            RefreshOverlay();
            return 0;
        }
        break;
    case WM_CTLCOLORSTATIC:
        if ((HWND)lParam == gStatus || (HWND)lParam == gChkAnnotate) {
            HDC hdc = (HDC)wParam;
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, GetSysColor(COLOR_WINDOWTEXT));
            return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
        }
        break;
    case WM_CTLCOLORBTN:
        if ((HWND)lParam == gChkAnnotate) {
            HDC hdc = (HDC)wParam;
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, GetSysColor(COLOR_WINDOWTEXT));
            return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
        }
        break;
    case WM_NOTIFY:
        return OnNotify(lParam);
    case WM_MOVE:
    case WM_WINDOWPOSCHANGED:
        RefreshOverlay();
        break;
    case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            DrawPreview(hdc);
            EndPaint(hwnd, &ps);
        }
        return 0;
    case WM_DESTROY:
        if (gOverlay) DestroyWindow(gOverlay);
        for (size_t i = 0; i < gNodeStore.size(); ++i) delete gNodeStore[i];
        gNodeStore.clear();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow) {
    EnableDpiAwareness();
    gInst = hInstance;
    INITCOMMONCONTROLSEX icc;
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_TREEVIEW_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icc);

    WNDCLASSW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"JeriVisionExplorerWindow";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"杰睿视觉分析工具", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, 1180, 760, NULL, NULL, hInstance, NULL);
    if (!hwnd) return 0;
    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return (int)msg.wParam;
}
