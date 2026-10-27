/* Copyright 2026 the Swiftleaf PDF authors.
   License: GPLv3 */

#include <oleacc.h>

struct RibbonAccessible : IAccessible {
    LONG refs = 1;
    HWND hwnd;
    VirtHost* instance;
    explicit RibbonAccessible(HWND h) : hwnd(h), instance((VirtHost*)GetWindowLongPtrW(h, GWLP_USERDATA)) {}
    VirtHost* Host() {
        return !hwnd ? instance
                     : (IsWindow(hwnd) && GetWindowLongPtrW(hwnd, GWLP_USERDATA) == (LONG_PTR)instance ? instance
                                                                                                       : nullptr);
    }
    static void Collect(ILayout* layout, Vec<VirtCtrl*>& out) {
        if (!layout || IsCollapsed(layout)) return;
        auto* w = layout->AsVirtCtrl();
        if (w && (w->HasFlag(vwfFocusable) || AsVirtText(w))) VecAppend(out, w);
        for (int i = 0; i < layout->LayoutChildCount(); i++) Collect(layout->LayoutChildAt(i), out);
    }
    static VirtCtrl* Find(ILayout* layout, int& id) {
        if (!layout || IsCollapsed(layout)) return nullptr;
        auto* w = layout->AsVirtCtrl();
        if (w && (w->HasFlag(vwfFocusable) || AsVirtText(w)) && --id == 0) return w;
        for (int i = 0; i < layout->LayoutChildCount(); i++) {
            if (auto* found = Find(layout->LayoutChildAt(i), id)) return found;
        }
        return nullptr;
    }
    VirtCtrl* Control(int id) {
        auto* host = Host();
        if (!host || id <= 0) return nullptr;
        return Find(host->layout, id);
    }
    int Count() {
        auto* host = Host();
        int id = 0;
        if (host) Find(host->layout, id);
        return -id;
    }
    int Child(VARIANT v) { return Host() && v.vt == VT_I4 && v.lVal >= 0 && v.lVal <= Count() ? v.lVal : -1; }
    static HRESULT Text(Str s, BSTR* out) {
        if (!out) return E_POINTER;
        *out = SysAllocString(CWStrTemp(s));
        return *out ? S_OK : E_OUTOFMEMORY;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (iid != __uuidof(IUnknown) && iid != __uuidof(IDispatch) && iid != __uuidof(IAccessible))
            return E_NOINTERFACE;
        *out = static_cast<IAccessible*>(this);
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&refs); }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG n = InterlockedDecrement(&refs);
        if (!n) delete this;
        return n;
    }
    HRESULT STDMETHODCALLTYPE GetTypeInfoCount(UINT* n) override {
        if (!n) return E_POINTER;
        *n = 0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetTypeInfo(UINT, LCID, ITypeInfo**) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetIDsOfNames(REFIID, LPOLESTR*, UINT, LCID, DISPID*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Invoke(DISPID, REFIID, LCID, WORD, DISPPARAMS*, VARIANT*, EXCEPINFO*, UINT*) override {
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE get_accParent(IDispatch** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        using CreateFn = HRESULT(WINAPI*)(HWND, LONG, REFIID, void**);
        static HMODULE dll = LoadLibraryExW(L"oleacc.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        static auto create = dll ? (CreateFn)GetProcAddress(dll, "CreateStdAccessibleObject") : nullptr;
        return Host() && create ? create(GetParent(hwnd), OBJID_WINDOW, IID_IDispatch, (void**)out) : S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE get_accChildCount(long* out) override {
        if (!out) return E_POINTER;
        *out = Count();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_accChild(VARIANT, IDispatch** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        return S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE get_accName(VARIANT v, BSTR* out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        int id = Child(v);
        if (id < 0) return E_INVALIDARG;
        if (!id) return Text(HwndGetTextTemp(hwnd), out);
        auto* w = Control(id);
        if (!w) return E_INVALIDARG;
        if (auto* b = AsRibbonButton(w)) return Text(len(b->label) ? b->label : b->tooltip, out);
        if (auto* t = AsRibbonTab(w)) return Text(t->label, out);
        if (auto* t = AsVirtText(w)) return Text(t->s, out);
        if (auto* t = AsRibbonSearchBox(w)) return Text(t->placeholder, out);
        return Text(w->tooltip, out);
    }
    HRESULT STDMETHODCALLTYPE get_accValue(VARIANT, BSTR* out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        return S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE get_accDescription(VARIANT v, BSTR* out) override {
        auto* w = Control(Child(v));
        return Text(w ? w->tooltip : Str{}, out);
    }
    HRESULT STDMETHODCALLTYPE get_accRole(VARIANT v, VARIANT* out) override {
        if (!out) return E_POINTER;
        int id = Child(v);
        if (id < 0) return E_INVALIDARG;
        VariantInit(out);
        out->vt = VT_I4;
        auto* w = Control(id);
        if (id && !w) return E_INVALIDARG;
        out->lVal = !id                        ? ROLE_SYSTEM_GROUPING
                    : AsRibbonTab(w)           ? ROLE_SYSTEM_PAGETAB
                    : AsRibbonSearchBox(w)     ? ROLE_SYSTEM_TEXT
                    : w->HasFlag(vwfFocusable) ? ROLE_SYSTEM_PUSHBUTTON
                                               : ROLE_SYSTEM_STATICTEXT;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_accState(VARIANT v, VARIANT* out) override {
        if (!out) return E_POINTER;
        int id = Child(v);
        if (id < 0) return E_INVALIDARG;
        VariantInit(out);
        out->vt = VT_I4;
        out->lVal = 0;
        auto* host = Host();
        if (!host) return E_INVALIDARG;
        if (!host->IsVisible()) out->lVal |= STATE_SYSTEM_INVISIBLE;
        auto* w = Control(id);
        if (!w) return S_OK;
        if (w->HasFlag(vwfFocusable)) out->lVal |= STATE_SYSTEM_FOCUSABLE;
        if (!w->IsEnabled()) out->lVal |= STATE_SYSTEM_UNAVAILABLE;
        if (w->HasFlag(vwfFocused) && host->HasFocus()) out->lVal |= STATE_SYSTEM_FOCUSED;
        if (auto* t = AsRibbonTab(w); t && t->isActive) out->lVal |= STATE_SYSTEM_SELECTED;
        if (auto* b = AsVirtIconButton(w); b && b->isSelected) out->lVal |= STATE_SYSTEM_CHECKED;
        if (w->VisibleRectInWindow().IsEmpty()) out->lVal |= STATE_SYSTEM_OFFSCREEN;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_accHelp(VARIANT, BSTR* out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        return S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE get_accHelpTopic(BSTR* out, VARIANT, long* topic) override {
        if (!out || !topic) return E_POINTER;
        *out = nullptr;
        *topic = -1;
        return S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE get_accKeyboardShortcut(VARIANT v, BSTR* out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        int id = Child(v);
        if (id <= 0) return S_FALSE;
        auto* w = Control(id);
        if (auto* sb = AsRibbonSearchBox(w); sb && len(sb->shortcut) > 0) {
            return Text(sb->shortcut, out);
        }
        return S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE get_accFocus(VARIANT* out) override {
        if (!out) return E_POINTER;
        VariantInit(out);
        auto* host = Host();
        if (!host || !host->HasFocus()) return S_FALSE;
        Vec<VirtCtrl*> controls;
        Collect(host->layout, controls);
        for (int i = 0; i < len(controls); i++)
            if (controls[i]->HasFlag(vwfFocused)) {
                out->vt = VT_I4;
                out->lVal = i + 1;
                return S_OK;
            }
        return S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE get_accSelection(VARIANT* out) override {
        if (!out) return E_POINTER;
        VariantInit(out);
        return S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE get_accDefaultAction(VARIANT v, BSTR* out) override {
        if (!out) return E_POINTER;
        auto* w = Control(Child(v));
        if (!w || !w->HasFlag(vwfFocusable)) {
            *out = nullptr;
            return S_FALSE;
        }
        return Text(Tr("Press"), out);
    }
    HRESULT STDMETHODCALLTYPE accSelect(long flags, VARIANT v) override {
        auto* w = Control(Child(v));
        if (!w || !w->IsEnabled() || !w->HasFlag(vwfFocusable) || !(flags & SELFLAG_TAKEFOCUS)) return E_INVALIDARG;
        HwndSetFocus(hwnd);
        auto* host = Host();
        w = Control(Child(v));
        if (!host || !host->vroot || !w || !w->IsEnabled()) return E_INVALIDARG;
        host->vroot->SetFocus(w);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE accLocation(long* x, long* y, long* dx, long* dy, VARIANT v) override {
        if (!x || !y || !dx || !dy) return E_POINTER;
        int id = Child(v);
        if (id < 0) return E_INVALIDARG;
        auto* host = Host();
        auto* w = Control(id);
        if (!host || (id && !w)) return E_INVALIDARG;
        Rect r = id ? host->ToScreen(w->BoundsInWindow()) : host->ScreenRect();
        *x = r.x;
        *y = r.y;
        *dx = r.dx;
        *dy = r.dy;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE accNavigate(long direction, VARIANT from, VARIANT* out) override {
        if (!out) return E_POINTER;
        VariantInit(out);
        int id = Child(from);
        if (id < 0) return E_INVALIDARG;
        int next = direction == NAVDIR_FIRSTCHILD && !id  ? 1
                   : direction == NAVDIR_LASTCHILD && !id ? Count()
                   : direction == NAVDIR_NEXT && id       ? id + 1
                   : direction == NAVDIR_PREVIOUS && id   ? id - 1
                                                          : -1;
        if (next <= 0 || next > Count()) return S_FALSE;
        out->vt = VT_I4;
        out->lVal = next;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE accHitTest(long x, long y, VARIANT* out) override {
        if (!out) return E_POINTER;
        VariantInit(out);
        auto* host = Host();
        if (!host || !host->IsVisible() || !host->ScreenRect().Contains({(int)x, (int)y})) return S_FALSE;
        out->vt = VT_I4;
        out->lVal = 0;
        Vec<VirtCtrl*> controls;
        Collect(host->layout, controls);
        for (int i = 0; i < len(controls); i++)
            if (host->ToScreen(controls[i]->VisibleRectInWindow()).Contains({(int)x, (int)y})) out->lVal = i + 1;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE accDoDefaultAction(VARIANT v) override {
        auto* w = Control(Child(v));
        auto* host = Host();
        if (!w || !w->IsEnabled() || !host || !host->IsVisible()) return E_INVALIDARG;
        VirtMouseEvent ev;
        ev.target = w;
        w->onClick.Call(&ev);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_accName(VARIANT, BSTR) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE put_accValue(VARIANT, BSTR) override { return E_NOTIMPL; }
};

static bool RibbonAccessMessage(VirtHostNativeMsg* ev) {
    if (ev->msg != WM_GETOBJECT || (LONG)ev->lp != OBJID_CLIENT) return false;
    using ResultFn = LRESULT(WINAPI*)(REFIID, WPARAM, LPUNKNOWN);
    static HMODULE dll = LoadLibraryExW(L"oleacc.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    static auto result = dll ? (ResultFn)GetProcAddress(dll, "LresultFromObject") : nullptr;
    if (!result) return false;
    auto* accessible = new RibbonAccessible(ev->host->native);
    ev->res = result(__uuidof(IAccessible), ev->wp, accessible);
    accessible->Release();
    ev->didHandle = true;
    return true;
}

static void RibbonAccessFocus(MainWindow*, VirtFocusEvent* ev) {
    if (!ev->gotFocus || !ev->w->root) return;
    HWND hwnd = ev->w->GetHwnd();
    RibbonAccessible accessible(hwnd);
    auto* host = accessible.Host();
    if (!host) return;
    Vec<VirtCtrl*> controls;
    RibbonAccessible::Collect(host->layout, controls);
    for (int i = 0; i < len(controls); i++) {
        if (controls[i] != ev->w) continue;
        NotifyWinEvent(EVENT_OBJECT_FOCUS, hwnd, OBJID_CLIENT, i + 1);
        return;
    }
}
