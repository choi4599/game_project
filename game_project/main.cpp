// ============================================================
//  JumpKing-Style Platformer  |  DirectX 11  |  Single File
//
//  ≠ �挫� 陛檜萄 ≠
//  式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式
//  [億 Ы概イ 寡纂]
//    BuildMap() 寰縑 и 還 蹺陛:
//    AddPlatform(顫殮, LX, BY, RX, TY);            // 欽儀
//    AddPlatform(顫殮, LX, BY, RX, TY, L"tex.png"); // 臢蝶籀
//
//  [億 Ы概イ 顫殮 蹺陛]
//    1. PlatformType 翮剪⑽縑 о跡 蹺陛
//    2. AddPlatform() 曖 switch 縑 晦獄 儀鼻 蹺陛
//    3. PlayerController::UpdateMovement() 曖
//       顫殮滌 碟晦(2欽啗)縑 檜翕 翕濛 蹺陛
//    ≦ 薄Щ 醞 檜翕 離欽(1欽啗)擎 濠翕 瞳辨脾
//
//  [億 闡ん凱お 蹺陛]
//    Component 蒂 鼻樓 ⊥ Start/Input/Update/Render 螃幗塭檜萄
//    錳ж朝 GameObject 縑 AddComponent() 煎 稱檜賊 部
//
//  [臢蝶籀]
//    褒чだ橾 蕙縑 PNG 寡纂 �� AddPlatform 葆雖虞 檣濠煎 唳煎 瞪殖
//    偽擎 だ橾擎 TexCache 陛 濠翕戲煎 醞犒 煎萄蒂 寞雖л
// 式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式
// 掘⑷ 頂辨 
// 2026.05.08
// 啪歜 瑞Щ 晦獄 掘褻, 蘋詭塭 蹺瞳
// 僭葬 : 醞溘, 陛樓紫 晦奩 雖賊 檜翕(譆堅樓紫 贗極Щ), 奢醞 婦撩 嶸雖 + 擒и 爾薑, 薄Щ 醱瞪 衛蝶蠱(�朴� ⊥ 萵葬鍔), 薄Щ 醞 檜翕 離欽(賅萇 顫殮 奢鱔), 奢醞 營薄Щ 寞雖, AABB 醱給 п唸(譆模 藹癱 寞щ 塵橫鹵)
// Ы滇イ : normal, ice, passThrough 
// 褻濛酈 : 寞щ酈(檜翕), space(薄Щ), f(fly), c/r(checkpoint), esc(謙猿), 葆辦蝶 謝贗葛(夔樂 謝ル 轎溘)
// ============================================================

#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <directxmath.h>
#include <wincodec.h>

#include <vector>
#include <string>
#include <unordered_map>
#include <chrono>
#include <algorithm>

#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(linker, "/entry:WinMainCRTStartup /subsystem:console")

using namespace DirectX;

// ============================================================
//  瞪寞 摹樹
// ============================================================
class GraphicsContext;
class GameObject;

// ============================================================
//  �飛� 鼻熱
// ============================================================
static constexpr int SCREEN_W = 800;
static constexpr int SCREEN_H = 600;

// ============================================================
//  僭葬 鼻熱  式  啪歜 替釵 褻薑擎 罹晦憮
// ============================================================
static constexpr float GRAVITY = -18.0f;
static constexpr float JUMP_MAX = 9.0f;
static constexpr float JUMP_MIN = 3.0f;
static constexpr float JUMP_CHARGE = 1.5f;
static constexpr float MOVE_SPEED = 2.0f;
static constexpr float ICE_ACCEL = 3.0f;
static constexpr float ICE_DRAG = 0.5f;
static constexpr float AIR_ACCEL = 2.0f;
static constexpr float BRAKE_ACCEL = 10.0f;
static constexpr float FLY_SPEED = 8.0f;
static constexpr float GROUND_ACCEL = 10.0f;  // 雖賊 陛樓紫 (堪擊熱煙 緒腦啪 譆堅樓 紫殖)
static constexpr float GROUND_DECEL = 20.0f;  // 雖賊 馬樓紫 (殮溘 橈擊 陽)

// ============================================================
//  晦獄 濠猿⑽
// ============================================================
struct Vec2 { float x = 0, y = 0; };

struct Vertex
{
    XMFLOAT3 pos;
    XMFLOAT2 uv;
    XMFLOAT4 col;
};

struct CbWorld { XMMATRIX  matWorld; };
struct CbMaterial { XMFLOAT4  tintColor; int useTexture; XMFLOAT3 pad; };
struct CbCamera { XMFLOAT2  offset;    XMFLOAT2 viewSize; };

// ============================================================
//  AABB
// ============================================================
struct AABB
{
    float left, right, bottom, top;
    bool Overlaps(const AABB& o) const
    {
        return left < o.right && right > o.left &&
            bottom < o.top && top > o.bottom;
    }
};

// ============================================================
//  Ы概イ 顫殮
//  ≠ 億 顫殮 蹺陛 衛:
//     1. 罹晦縑 翮剪高 蹺陛
//     2. AddPlatform() switch 縑 晦獄儀 蹺陛
//     3. UpdateMovement() 2欽啗 碟晦縑 檜翕 翕濛 蹺陛
// ============================================================
enum class PlatformType { Normal, Ice, PassThrough };

// ============================================================
//  ShaderSet
// ============================================================
struct ShaderSet
{
    ID3D11VertexShader* vs = nullptr;
    ID3D11PixelShader* ps = nullptr;
    ID3D11InputLayout* layout = nullptr;

    void Release()
    {
        if (vs) { vs->Release();     vs = nullptr; }
        if (ps) { ps->Release();     ps = nullptr; }
        if (layout) { layout->Release(); layout = nullptr; }
    }
};

// ============================================================
//  DeltaTime
// ============================================================
class DeltaTime
{
    std::chrono::high_resolution_clock::time_point prev;
public:
    DeltaTime() { prev = std::chrono::high_resolution_clock::now(); }
    float Get()
    {
        auto  now = std::chrono::high_resolution_clock::now();
        float dt = std::chrono::duration<float>(now - prev).count();
        prev = now;
        return min(dt, 0.05f);
    }
};

// ============================================================
//  WindowContext
// ============================================================
class WindowContext
{
public:
    HWND hWnd = nullptr;
    int  Width = SCREEN_W, Height = SCREEN_H;

    bool Initialize(HINSTANCE hInst,
        LRESULT(CALLBACK* proc)(HWND, UINT, WPARAM, LPARAM))
    {
        WNDCLASSEX wc = { sizeof(WNDCLASSEX) };
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = proc;
        wc.hInstance = hInst;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.lpszClassName = L"JumpGame";
        if (!RegisterClassEx(&wc)) return false;

        RECT rc = { 0, 0, Width, Height };
        AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
        hWnd = CreateWindow(L"JumpGame",
            L"Jump Game  [Space:Jump  Arrow:Move  F:Fly  R:Reset  C:Checkpoint  LClick:Coord]",
            WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
            rc.right - rc.left, rc.bottom - rc.top,
            NULL, NULL, hInst, NULL);
        if (!hWnd) return false;
        ShowWindow(hWnd, SW_SHOW);
        return true;
    }
    ~WindowContext() { UnregisterClass(L"JumpGame", GetModuleHandle(NULL)); }
};

// ============================================================
//  GraphicsContext
// ============================================================
class GraphicsContext
{
public:
    ID3D11Device* Device = nullptr;
    ID3D11DeviceContext* Context = nullptr;
    IDXGISwapChain* SwapChain = nullptr;
    ID3D11RenderTargetView* RTV = nullptr;
    ID3D11SamplerState* Sampler = nullptr;

    bool Init(HWND hWnd, int w, int h)
    {
        DXGI_SWAP_CHAIN_DESC sd = {};
        sd.BufferCount = 1;
        sd.BufferDesc.Width = w;
        sd.BufferDesc.Height = h;
        sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.OutputWindow = hWnd;
        sd.SampleDesc.Count = 1;
        sd.Windowed = TRUE;

        HRESULT hr = D3D11CreateDeviceAndSwapChain(
            NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0,
            NULL, 0, D3D11_SDK_VERSION, &sd,
            &SwapChain, &Device, NULL, &Context);
        if (FAILED(hr)) return false;
        if (!CreateRTV()) return false;

        // ん檣お 價Ы楝 (а撚嬴お 纔舒葬 廓颶 寞雖)
        D3D11_SAMPLER_DESC smpDesc = {};
        smpDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
        smpDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
        smpDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
        smpDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
        Device->CreateSamplerState(&smpDesc, &Sampler);

        // 憲だ 綰溶萄 (PNG 癱貲 籀葬)
        D3D11_BLEND_DESC bd = {};
        bd.RenderTarget[0].BlendEnable = TRUE;
        bd.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
        bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
        bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
        bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        ID3D11BlendState* bs = nullptr;
        Device->CreateBlendState(&bd, &bs);
        float bf[4] = {};
        Context->OMSetBlendState(bs, bf, 0xFFFFFFFF);
        bs->Release();

        return true;
    }

    bool CreateRTV()
    {
        if (RTV) { RTV->Release(); RTV = nullptr; }
        ID3D11Texture2D* bb = nullptr;
        SwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&bb);
        HRESULT hr = Device->CreateRenderTargetView(bb, NULL, &RTV);
        bb->Release();
        return SUCCEEDED(hr);
    }

    ShaderSet CompileShaders(const char* src,
        D3D11_INPUT_ELEMENT_DESC* ied, UINT iedCount)
    {
        ShaderSet result;
        ID3DBlob* vsBlob = nullptr, * psBlob = nullptr, * err = nullptr;
        size_t len = strlen(src);

        if (FAILED(D3DCompile(src, len, nullptr, nullptr, nullptr,
            "VS", "vs_5_0", 0, 0, &vsBlob, &err)))
        {
            if (err) { OutputDebugStringA((char*)err->GetBufferPointer()); err->Release(); }
            return result;
        }
        if (FAILED(D3DCompile(src, len, nullptr, nullptr, nullptr,
            "PS", "ps_5_0", 0, 0, &psBlob, &err)))
        {
            if (err) { OutputDebugStringA((char*)err->GetBufferPointer()); err->Release(); }
            if (vsBlob) vsBlob->Release();
            return result;
        }

        Device->CreateVertexShader(vsBlob->GetBufferPointer(),
            vsBlob->GetBufferSize(), nullptr, &result.vs);
        Device->CreatePixelShader(psBlob->GetBufferPointer(),
            psBlob->GetBufferSize(), nullptr, &result.ps);
        Device->CreateInputLayout(ied, iedCount,
            vsBlob->GetBufferPointer(),
            vsBlob->GetBufferSize(), &result.layout);
        vsBlob->Release(); psBlob->Release();
        return result;
    }

    ~GraphicsContext()
    {
        if (Sampler)   Sampler->Release();
        if (RTV)       RTV->Release();
        if (SwapChain) SwapChain->Release();
        if (Context)   Context->Release();
        if (Device)    Device->Release();
    }
};

// ============================================================
//  臢蝶籀 煎渦 (WIC)
// ============================================================
ID3D11ShaderResourceView* LoadTextureFromFile(GraphicsContext* gfx,
    const wchar_t* path)
{
    IWICImagingFactory* factory = nullptr;
    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* converter = nullptr;

    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr,
        CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))))
        return nullptr;

    if (FAILED(factory->CreateDecoderFromFilename(path, nullptr,
        GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder)))
    {
        printf("[Texture] だ橾 翮晦 褒ぬ: %ls\n", path);
        factory->Release();
        return nullptr;
    }

    decoder->GetFrame(0, &frame);
    factory->CreateFormatConverter(&converter);
    converter->Initialize(frame, GUID_WICPixelFormat32bppRGBA,
        WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);

    UINT w = 0, h = 0;
    converter->GetSize(&w, &h);
    std::vector<BYTE> pixels(w * h * 4);
    converter->CopyPixels(nullptr, w * 4, (UINT)pixels.size(), pixels.data());

    D3D11_TEXTURE2D_DESC td = {};
    td.Width = w; td.Height = h; td.MipLevels = 1; td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA sd = {};
    sd.pSysMem = pixels.data();
    sd.SysMemPitch = w * 4;

    ID3D11Texture2D* tex = nullptr;
    ID3D11ShaderResourceView* srv = nullptr;
    gfx->Device->CreateTexture2D(&td, &sd, &tex);
    gfx->Device->CreateShaderResourceView(tex, nullptr, &srv);

    tex->Release(); converter->Release();
    frame->Release(); decoder->Release(); factory->Release();

    printf("[Texture] 煎萄 撩奢: %ls (%ux%u)\n", path, w, h);
    return srv;
}

// ============================================================
//  TextureCache
//  式 偽擎 だ橾 唳煎朝 и 廓虜 煎萄м棲棻.
//  式 模資濠縑憮 賅萇 SRV 蒂 橾婪 п薯м棲棻.
// ============================================================
class TextureCache
{
    GraphicsContext* gfx = nullptr;
    std::unordered_map<std::wstring, ID3D11ShaderResourceView*> cache;
public:
    void Init(GraphicsContext* g) { gfx = g; }

    // path == nullptr 檜賊 nullptr 奩�� (臢蝶籀 橈擠 ⊥ 欽儀 ィ寥)
    ID3D11ShaderResourceView* Get(const wchar_t* path)
    {
        if (!path) return nullptr;
        std::wstring key(path);
        auto it = cache.find(key);
        if (it != cache.end()) return it->second;
        auto* srv = LoadTextureFromFile(gfx, path);
        cache[key] = srv;
        return srv;
    }

    ~TextureCache()
    {
        for (auto& pair : cache)
            if (pair.second) pair.second->Release();
    }
};

// ============================================================
//  Mesh
// ============================================================
class Mesh
{
public:
    ID3D11Buffer* VB = nullptr;
    UINT          Count = 0;

    void Create(GraphicsContext* gfx, const std::vector<Vertex>& verts)
    {
        if (VB) { VB->Release(); VB = nullptr; }
        Count = (UINT)verts.size();

        D3D11_BUFFER_DESC bd = {};
        bd.Usage = D3D11_USAGE_DEFAULT;
        bd.ByteWidth = sizeof(Vertex) * Count;
        bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;

        D3D11_SUBRESOURCE_DATA sd = {};
        sd.pSysMem = verts.data();
        gfx->Device->CreateBuffer(&bd, &sd, &VB);
    }

    void CreateQuad(GraphicsContext* gfx,
        float lx, float by, float rx, float ty,
        XMFLOAT4 col = { 1,1,1,1 })
    {
        std::vector<Vertex> v =
        {
            { {lx, ty, 0}, {0,0}, col },
            { {rx, ty, 0}, {1,0}, col },
            { {lx, by, 0}, {0,1}, col },
            { {rx, ty, 0}, {1,0}, col },
            { {rx, by, 0}, {1,1}, col },
            { {lx, by, 0}, {0,1}, col },
        };
        Create(gfx, v);
    }

    ~Mesh() { if (VB) VB->Release(); }
};

// ============================================================
//  Material 晦奩
// ============================================================
class Material
{
public:
    ShaderSet Shaders;
    explicit Material(ShaderSet s) : Shaders(s) {}
    virtual ~Material() {}
    virtual void Bind(GraphicsContext* gfx) = 0;
};

// 式式 ColorMaterial : 欽儀 傳朝 臢蝶籀 式式式式式式式式式式式式式式式式式式式式式式
class ColorMaterial : public Material
{
public:
    XMFLOAT4                  Color = { 1,1,1,1 };
    ID3D11Buffer* CB = nullptr;
    ID3D11ShaderResourceView* TexSRV = nullptr;

    ColorMaterial(ShaderSet s, XMFLOAT4 col, GraphicsContext* gfx)
        : Material(s), Color(col)
    {
        D3D11_BUFFER_DESC bd = {};
        bd.Usage = D3D11_USAGE_DEFAULT;
        bd.ByteWidth = sizeof(CbMaterial);
        bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        gfx->Device->CreateBuffer(&bd, nullptr, &CB);
    }

    // 臢蝶籀蒂 翱唸ж賊 tintColor 蒂 �羃�(醞董)戲煎 濠翕 滲唳
    void SetTexture(ID3D11ShaderResourceView* srv)
    {
        TexSRV = srv;
        if (srv) Color = { 1,1,1,1 };
    }
    void SetColor(XMFLOAT4 col) { Color = col; }

    void Bind(GraphicsContext* gfx) override
    {
        gfx->Context->IASetInputLayout(Shaders.layout);
        gfx->Context->VSSetShader(Shaders.vs, nullptr, 0);
        gfx->Context->PSSetShader(Shaders.ps, nullptr, 0);

        CbMaterial cb = { Color, (TexSRV ? 1 : 0), {0,0,0} };
        gfx->Context->UpdateSubresource(CB, 0, nullptr, &cb, 0, 0);
        gfx->Context->PSSetConstantBuffers(1, 1, &CB);
        gfx->Context->PSSetShaderResources(0, 1, &TexSRV);
        gfx->Context->PSSetSamplers(0, 1, &gfx->Sampler);
    }

    ~ColorMaterial() override { if (CB) CB->Release(); }
};

// ============================================================
//  Component / GameObject
// ============================================================
class Component
{
public:
    GameObject* Owner = nullptr;
    bool        Started = false;

    virtual void Start(GraphicsContext* gfx) {}
    virtual void Input() {}
    virtual void Update(float dt) {}
    virtual void Render(GraphicsContext* gfx) {}
    virtual ~Component() {}
};

class GameObject
{
public:
    Vec2  Pos = { 0, 0 };
    float Rot = 0.0f;
    Vec2  Scale = { 1, 1 };
    bool  Active = true;

    std::vector<Component*> Components;

    GameObject(float x = 0, float y = 0) { Pos = { x, y }; }
    ~GameObject() { for (auto* c : Components) delete c; }

    void AddComponent(Component* c) { c->Owner = this; Components.push_back(c); }

    template<typename T>
    T* GetComponent()
    {
        for (auto* c : Components)
            if (auto* p = dynamic_cast<T*>(c)) return p;
        return nullptr;
    }

    void Input()
    {
        if (!Active) return;
        for (auto* c : Components) c->Input();
    }
    void Update(float dt, GraphicsContext* gfx)
    {
        if (!Active) return;
        for (auto* c : Components)
        {
            if (!c->Started) { c->Start(gfx); c->Started = true; }
            c->Update(dt);
        }
    }
    void Render(GraphicsContext* gfx)
    {
        if (!Active) return;
        for (auto* c : Components) c->Render(gfx);
    }
};

// ============================================================
//  MeshRenderer
// ============================================================
class MeshRenderer : public Component
{
public:
    Mesh* pMesh = nullptr;
    Material* pMat = nullptr;
    ID3D11Buffer* CB = nullptr;

    MeshRenderer(Mesh* mesh, Material* mat) : pMesh(mesh), pMat(mat) {}

    void Start(GraphicsContext* gfx) override
    {
        D3D11_BUFFER_DESC bd = {};
        bd.Usage = D3D11_USAGE_DEFAULT;
        bd.ByteWidth = sizeof(CbWorld);
        bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        gfx->Device->CreateBuffer(&bd, nullptr, &CB);
    }

    void Render(GraphicsContext* gfx) override
    {
        if (!pMesh || !pMat || !CB) return;
        pMat->Bind(gfx);

        XMMATRIX world =
            XMMatrixScaling(Owner->Scale.x, Owner->Scale.y, 1.0f) *
            XMMatrixRotationZ(Owner->Rot) *
            XMMatrixTranslation(Owner->Pos.x, Owner->Pos.y, 0.0f);

        CbWorld cb = { XMMatrixTranspose(world) };
        gfx->Context->UpdateSubresource(CB, 0, nullptr, &cb, 0, 0);
        gfx->Context->VSSetConstantBuffers(0, 1, &CB);

        UINT stride = sizeof(Vertex), offset = 0;
        gfx->Context->IASetVertexBuffers(0, 1, &pMesh->VB, &stride, &offset);
        gfx->Context->Draw(pMesh->Count, 0);
    }

    ~MeshRenderer() override { if (CB) CB->Release(); }
};

// ============================================================
//  Camera
// ============================================================
class Camera
{
public:
    Vec2  Pos = { 0, 0 };
    float ViewW = 0;
    float ViewH = 0;
    ID3D11Buffer* CB = nullptr;

    void Init(GraphicsContext* gfx, int w, int h)
    {
        ViewW = (float)w; ViewH = (float)h;
        D3D11_BUFFER_DESC bd = {};
        bd.Usage = D3D11_USAGE_DEFAULT;
        bd.ByteWidth = sizeof(CbCamera);
        bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        gfx->Device->CreateBuffer(&bd, nullptr, &CB);
    }

    void Follow(Vec2 target, float dt)
    {
        float s = 5.0f;
        Pos.x += (target.x - Pos.x) * s * dt;
        Pos.y += (target.y - Pos.y) * s * dt;
    }

    void Upload(GraphicsContext* gfx)
    {
        CbCamera cb = { {Pos.x, Pos.y}, {ViewW, ViewH} };
        gfx->Context->UpdateSubresource(CB, 0, nullptr, &cb, 0, 0);
        gfx->Context->VSSetConstantBuffers(2, 1, &CB);
    }

    Vec2 ScreenToWorld(int sx, int sy) const
    {
        float nx = (float)sx / ViewW * 2.0f - 1.0f;
        float ny = 1.0f - (float)sy / ViewH * 2.0f;
        float hw = ViewW / 200.0f, hh = ViewH / 200.0f;
        return { Pos.x + nx * hw, Pos.y + ny * hh };
    }

    ~Camera() { if (CB) CB->Release(); }
};

// ============================================================
//  PlatformComp
//  式 AABB 蒂 GameObject 曖 Pos/Scale 縑憮 啗骯м棲棻.
//    檜溢啪 ж賊 釭醞縑 Ы概イ擊 遺霜罹紫 醱給檜 評塭褫棲棻.
// ============================================================
class PlatformComp : public Component
{
public:
    PlatformType Type;

    PlatformComp(PlatformType t) : Type(t) {}

    AABB GetAABB() const
    {
        float hw = Owner->Scale.x * 0.5f;
        float hh = Owner->Scale.y * 0.5f;
        return {
            Owner->Pos.x - hw,
            Owner->Pos.x + hw,
            Owner->Pos.y - hh,
            Owner->Pos.y + hh
        };
    }
};

// ============================================================
//  PlayerController
//  式 Platforms ん檣攪蒂 諼睡縑憮 輿殮嫡蝗棲棻 (瞪羲 薯剪).
//  式 檜翕 籀葬蒂 UpdateMovement() 煎 碟葬ц蝗棲棻.
//    薄Щ 醱瞪 醞 檜翕 離欽擎 顫殮婁 鼠婦ж啪 1欽啗縑憮 籀葬腎嘎煎
//    億 顫殮擊 蹺陛п紫 檜 翕濛擎 濠翕戲煎 瞳辨腌棲棻.
// ============================================================
class PlayerController : public Component
{
public:
    // 式式 諼睡 輿殮 式式
    std::vector<GameObject*>* Platforms = nullptr;

    // 式式 僭葬 鼻鷓 式式
    Vec2  Vel = { 0, 0 };
    bool  OnGround = false;
    float HalfW = 0.3f;
    float HalfH = 0.3f;

    // 式式 薄Щ 式式
    bool  SpaceHeld = false;
    float JumpCharge = 0.0f;
    bool  JumpedThisPress = false;

    // 式式 殮溘 式式
    float MoveInput = 0.0f;

    // 式式 雖賊 顫殮 Ы楚斜 式式
    bool  OnIce = false;

    // 式式 偃嫦 紫掘 式式
    bool  FlyMode = false;
    Vec2  Checkpoint = { 0, 0.5f };

    // 式式 縉雖 馬雖 式式
    bool PrevF = false, PrevR = false, PrevC = false;

    // 式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式
    void Input() override
    {
        bool spaceNow = (GetAsyncKeyState(VK_SPACE) & 0x8000) != 0;
        bool fNow = (GetAsyncKeyState('F') & 0x8000) != 0;
        bool rNow = (GetAsyncKeyState('R') & 0x8000) != 0;
        bool cNow = (GetAsyncKeyState('C') & 0x8000) != 0;

        SpaceHeld = spaceNow;

        // 寞щ 殮溘擎 о鼻 檗擠
        // (醱瞪 醞縑朝 Update 縑憮 檜翕縑 奩艙ж雖 彊堅 嫦餌 寞щ縑虜 餌辨)
        MoveInput = 0.0f;
        if (GetAsyncKeyState(VK_LEFT) & 0x8000) MoveInput -= 1.0f;
        if (GetAsyncKeyState(VK_RIGHT) & 0x8000) MoveInput += 1.0f;

        if (fNow && !PrevF)
        {
            FlyMode = !FlyMode; Vel = { 0, 0 };
            printf("[Dev] FlyMode: %s\n", FlyMode ? "ON" : "OFF");
        }
        if (rNow && !PrevR)
        {
            Owner->Pos = Checkpoint; Vel = { 0, 0 };
            printf("[Dev] Reset (%.2f, %.2f)\n", Checkpoint.x, Checkpoint.y);
        }
        if (cNow && !PrevC)
        {
            Checkpoint = Owner->Pos;
            printf("[Dev] Checkpoint (%.2f, %.2f)\n", Checkpoint.x, Checkpoint.y);
        }

        PrevF = fNow; PrevR = rNow; PrevC = cNow;
    }

    // 式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式
    void Update(float dt) override
    {
        if (FlyMode) { UpdateFly(dt); return; }

        Vel.y += GRAVITY * dt;

        UpdateMovement(dt);
        UpdateJump(dt);

        Owner->Pos.x += Vel.x * dt;
        Owner->Pos.y += Vel.y * dt;

        ResolveAllCollisions();
    }

private:
    // 式式 熱ゎ 檜翕
    //
    //  [1欽啗] 薄Щ 醱瞪 醞檜賊 顫殮縑 鼻婦橈檜 檜翕 離欽
    //          ⊥ 億 顫殮擊 蹺陛п紫 罹晦朝 勒萄萵 в蹂 橈擠
    //
    //  [2欽啗] 醱瞪 醞檜 嬴棍 陽 顫殮滌 檜翕 翕濛
    //          ⊥ 億 顫殮 蹺陛 衛 else if 綰煙虜 蹺陛
    // 式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式
    void UpdateMovement(float dt)
    {
        // 式式 1欽啗: 醱瞪 醞 檜翕 離欽 (賅萇 顫殮 奢鱔) 式式
        if (SpaceHeld && OnGround)
        {
            if (OnIce)
                Vel.x *= (1.0f - ICE_DRAG * dt);   // 橡擠擎 嘐莖楝雖貊 馬樓
            else
                Vel.x += (0.0f - Vel.x) * BRAKE_ACCEL * dt; // 橾奩擎 粽溯檜觼
            return;
        }

        // 式式 2欽啗: 顫殮滌 檜翕 式式
        // ≠ 億 顫殮 蹺陛 衛 else if 綰煙擊 罹晦縑 蹺陛ж撮蹂.
        if (OnIce)
        {
            if (MoveInput != 0.0f)
            {
                Vel.x += MoveInput * ICE_ACCEL * dt;
                Vel.x = max(-MOVE_SPEED, min(MOVE_SPEED, Vel.x));
            }
            else
            {
                Vel.x *= (1.0f - ICE_DRAG * dt);
            }
        }
        else if (OnGround)
        {
            // 滲唳 瞪: Vel.x = MoveInput * MOVE_SPEED;  (闊陝 奩擬)

            // 滲唳 ��: 陛樓/馬樓
            if (MoveInput != 0.0f)
            {
                Vel.x += MoveInput * GROUND_ACCEL * dt;
                Vel.x = max(-MOVE_SPEED, min(MOVE_SPEED, Vel.x));
            }
            else
            {
                // 殮溘 橈戲賊 馬樓
                float decel = GROUND_DECEL * dt;
                if (Vel.x > 0) Vel.x = max(0.0f, Vel.x - decel);
                else           Vel.x = min(0.0f, Vel.x + decel);
            }
        }
    }

    // 式式 薄Щ 醱瞪 & 嫦餌 式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式
    void UpdateJump(float dt)
    {
        if (!OnGround)
        {
            // 雜雖 瞪梱雖 JumpedThisPress 嶸雖 (奢醞 營薄Щ 寞雖)
            if (!SpaceHeld) JumpedThisPress = false;
            return;
        }

        if (SpaceHeld && !JumpedThisPress)
        {
            JumpCharge = min(JumpCharge + JUMP_CHARGE * dt, 1.0f);
        }
        else if (!SpaceHeld && JumpCharge > 0.0f)
        {
            float speed = JUMP_MIN + (JUMP_MAX - JUMP_MIN) * JumpCharge;
            Vel.y = speed;
            Vel.x = MoveInput * MOVE_SPEED; // 婦撩 橈檜 竣橫噪
            JumpCharge = 0.0f;
            JumpedThisPress = true;
            OnGround = false;
        }

        if (!SpaceHeld && OnGround) JumpedThisPress = false;
    }

    // 式式 醱給 п唸 式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式
    void ResolveAllCollisions()
    {
        OnGround = false;
        OnIce = false;

        if (!Platforms) return;
        for (auto* go : *Platforms)
        {
            auto* plat = go->GetComponent<PlatformComp>();
            if (plat) ResolveCollision(plat);
        }
    }

    void ResolveCollision(PlatformComp* plat)
    {
        AABB player = GetAABB();
        AABB platform = plat->GetAABB();
        if (!player.Overlaps(platform)) return;

        float oL = player.right - platform.left;
        float oR = platform.right - player.left;
        float oB = player.top - platform.bottom;
        float oT = platform.top - player.bottom;

        if (plat->Type == PlatformType::PassThrough)
        {
            float prevBottom = Owner->Pos.y - HalfH - Vel.y * 0.016f;
            if (prevBottom >= platform.top - 0.01f && Vel.y <= 0.0f)
            {
                Owner->Pos.y = platform.top + HalfH;
                Vel.y = 0;
                OnGround = true;
            }
            return;
        }

        float minO = min(min(oL, oR), min(oB, oT));
        if (minO == oT) { Owner->Pos.y = platform.top + HalfH; if (Vel.y < 0) Vel.y = 0; OnGround = true; if (plat->Type == PlatformType::Ice) OnIce = true; }
        else if (minO == oB) { Owner->Pos.y = platform.bottom - HalfH; if (Vel.y > 0) Vel.y = 0; }
        else if (minO == oL) { Owner->Pos.x = platform.left - HalfW; Vel.x = 0; }
        else { Owner->Pos.x = platform.right + HalfW; Vel.x = 0; }
    }

    void UpdateFly(float dt)
    {
        Vec2 dir = { 0, 0 };
        if (GetAsyncKeyState(VK_LEFT) & 0x8000) dir.x -= 1.0f;
        if (GetAsyncKeyState(VK_RIGHT) & 0x8000) dir.x += 1.0f;
        if (GetAsyncKeyState(VK_UP) & 0x8000) dir.y += 1.0f;
        if (GetAsyncKeyState(VK_DOWN) & 0x8000) dir.y -= 1.0f;
        Owner->Pos.x += dir.x * FLY_SPEED * dt;
        Owner->Pos.y += dir.y * FLY_SPEED * dt;
        Vel = { 0, 0 };
    }

    AABB GetAABB() const
    {
        return {
            Owner->Pos.x - HalfW, Owner->Pos.x + HalfW,
            Owner->Pos.y - HalfH, Owner->Pos.y + HalfH
        };
    }
};

// ============================================================
//  JumpChargeBar  式  醱瞪榆 衛陝��
// ============================================================
class JumpChargeBar : public Component
{
    Mesh* pMesh = nullptr;
    Material* pMat = nullptr;
    ID3D11Buffer* CB = nullptr;
public:
    JumpChargeBar(Mesh* m, Material* mat) : pMesh(m), pMat(mat) {}

    void Start(GraphicsContext* gfx) override
    {
        D3D11_BUFFER_DESC bd = {};
        bd.Usage = D3D11_USAGE_DEFAULT;
        bd.ByteWidth = sizeof(CbWorld);
        bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        gfx->Device->CreateBuffer(&bd, nullptr, &CB);
    }

    void Render(GraphicsContext* gfx) override
    {
        auto* pc = Owner->GetComponent<PlayerController>();
        if (!pc || pc->FlyMode || !pc->OnGround || pc->JumpCharge <= 0.0f) return;

        pMat->Bind(gfx);

        XMMATRIX world =
            XMMatrixScaling(pc->JumpCharge * 0.6f, 0.05f, 1.0f) *
            XMMatrixTranslation(Owner->Pos.x - 0.3f, Owner->Pos.y + 0.45f, 0.0f);

        CbWorld cb = { XMMatrixTranspose(world) };
        gfx->Context->UpdateSubresource(CB, 0, nullptr, &cb, 0, 0);
        gfx->Context->VSSetConstantBuffers(0, 1, &CB);

        UINT stride = sizeof(Vertex), offset = 0;
        gfx->Context->IASetVertexBuffers(0, 1, &pMesh->VB, &stride, &offset);
        gfx->Context->Draw(pMesh->Count, 0);
    }

    ~JumpChargeBar() override { if (CB) CB->Release(); }
};

// ============================================================
//  GameLoop
// ============================================================
class GameLoop
{
public:
    WindowContext            Win;
    GraphicsContext          Gfx;
    DeltaTime                Timer;
    Camera                   Cam;
    TextureCache             TexCache;
    bool                     Running = true;

    std::vector<GameObject*> World;
    std::vector<GameObject*> Platforms;

    // 式式 奢嶸 GPU 葬模蝶 式式
    ShaderSet      DefaultShaders;
    Mesh* QuadMesh = nullptr;   // 欽嬪 蘭萄 [-0.5, 0.5]
    Mesh* BarMesh = nullptr;   // 醱瞪 夥 蘭萄 [0, 1]

    // 式式 Ы溯檜橫 瞪辨 該じ葬橡 (GameLoop 模嶸) 式式
    ColorMaterial* MatPlayer = nullptr;
    ColorMaterial* MatChargeBar = nullptr;

    // 式式 Ы概イ 該じ葬橡 跡煙 (AddPlatform 縑憮 儅撩, 罹晦憮 模嶸) 式式
    std::vector<Material*>   OwnedMaterials;

    bool MousePressed = false;

    GameLoop() { printf("[Engine] GameLoop Created.\n"); }
    ~GameLoop()
    {
        for (auto* go : World)     delete go;
        for (auto* go : Platforms) delete go;
        for (auto* m : OwnedMaterials) delete m;
        // TexCache 模資濠陛 SRV 濠翕 п薯
        CoUninitialize();
        delete MatPlayer;
        delete MatChargeBar;
        delete QuadMesh;
        delete BarMesh;
        DefaultShaders.Release();
        printf("[Engine] All resources released.\n");
    }

    // 式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式
    bool Initialize(HINSTANCE hInst,
        LRESULT(CALLBACK* proc)(HWND, UINT, WPARAM, LPARAM))
    {
        if (!Win.Initialize(hInst, proc))      return false;
        if (!Gfx.Init(Win.hWnd, SCREEN_W, SCREEN_H)) return false;

        Cam.Init(&Gfx, SCREEN_W, SCREEN_H);
        CoInitialize(nullptr);
        TexCache.Init(&Gfx);

        // 式式 樁檜渦 式式
        const char* shaderSrc = R"(
            cbuffer cbWorld    : register(b0) { matrix matWorld; };
            cbuffer cbMaterial : register(b1) { float4 tintColor; int useTexture; float3 pad; };
            cbuffer cbCamera   : register(b2) { float2 camOffset; float2 viewSize; };
            Texture2D    gTex    : register(t0);
            SamplerState gSampler: register(s0);

            struct VS_IN { float3 pos:POSITION; float2 uv:TEXCOORD; float4 col:COLOR; };
            struct PS_IN { float4 pos:SV_POSITION; float2 uv:TEXCOORD; float4 col:COLOR; };

            PS_IN VS(VS_IN i)
            {
                PS_IN o;
                float4 w = mul(float4(i.pos, 1.0f), matWorld);
                o.pos = float4(
                    (w.x - camOffset.x) / (viewSize.x / 200.0f),
                    (w.y - camOffset.y) / (viewSize.y / 200.0f),
                    0.0f, 1.0f);
                o.uv  = i.uv;
                o.col = i.col;
                return o;
            }
            float4 PS(PS_IN i) : SV_Target
            {
                if (useTexture) return gTex.Sample(gSampler, i.uv) * tintColor;
                return tintColor;
            }
        )";

        D3D11_INPUT_ELEMENT_DESC ied[] =
        {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0,  0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 20, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };
        DefaultShaders = Gfx.CompileShaders(shaderSrc, ied, 3);

        // 式式 詭蓮 式式
        QuadMesh = new Mesh();
        QuadMesh->CreateQuad(&Gfx, -0.5f, -0.5f, 0.5f, 0.5f);

        BarMesh = new Mesh();
        BarMesh->CreateQuad(&Gfx, 0.0f, 0.0f, 1.0f, 1.0f);

        // 式式 Ы溯檜橫 該じ葬橡 式式
        MatPlayer = new ColorMaterial(DefaultShaders, { 0.3f, 0.6f, 1.0f, 1.0f }, &Gfx);
        MatChargeBar = new ColorMaterial(DefaultShaders, { 1.0f, 0.9f, 0.0f, 1.0f }, &Gfx);

        auto* playerTex = TexCache.Get(L"player.png");
        if (playerTex) MatPlayer->SetTexture(playerTex);

        BuildMap();
        BuildPlayer();

        printf("[Engine] Ready.\n");
        printf("  Arrow L/R : Move\n");
        printf("  Space     : Jump (hold = charge, release = launch)\n");
        printf("  F : Fly   R : Reset   C : Checkpoint   ESC : Quit\n");
        printf("  LClick : Print world coord\n");
        return true;
    }

    // 式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式
    //  裘 掘撩
    //  ≠ Ы概イ 蹺陛/薯剪朝 罹晦憮虜 ж賊 腌棲棻.
    //
    //  欽儀:    AddPlatform(顫殮, LX, BY, RX, TY);
    //  臢蝶籀:  AddPlatform(顫殮, LX, BY, RX, TY, L"だ橾.png");
    // 式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式
    void BuildMap()
    {
        // 式式 夥款 式式
        AddPlatform(PlatformType::Normal, -5.0f, -3.0f, 5.0f, 0.0f, L"ground2.png");

        // 式式 1類 式式
        AddPlatform(PlatformType::Normal, -3.0f, 1.5f, 0.0f, 1.8f, L"ground1.png");
        AddPlatform(PlatformType::Ice, 1.0f, 1.5f, 3.5f, 1.8f, L"ice1.png");
        AddPlatform(PlatformType::Normal, -1.0f, 3.5f, 2.0f, 3.8f, L"ground1.png");

        // 式式 2類 式式
        AddPlatform(PlatformType::Normal, -4.0f, 5.0f, -1.5f, 5.3f, L"ground1.png");
        AddPlatform(PlatformType::Ice, 0.0f, 5.5f, 3.0f, 5.8f, L"ice1.png");
        AddPlatform(PlatformType::PassThrough, -2.0f, 7.0f, 0.5f, 7.2f, L"passThrough1.png");
        AddPlatform(PlatformType::Normal, 1.5f, 7.0f, 4.0f, 7.3f, L"ground1.png");

        // 式式 3類 式式
        AddPlatform(PlatformType::Normal, -3.5f, 9.0f, -2.0f, 9.2f);
        AddPlatform(PlatformType::Ice, -0.5f, 9.5f, 1.5f, 9.7f, L"ice1.png");
        AddPlatform(PlatformType::Normal, 2.5f, 10.0f, 4.5f, 10.2f, L"ground1.png");
        AddPlatform(PlatformType::PassThrough, -1.0f, 11.5f, 1.5f, 11.7f);

        // 式式 4類 式式
        AddPlatform(PlatformType::Normal, -4.0f, 13.0f, -1.0f, 13.3f);
        AddPlatform(PlatformType::Ice, 0.5f, 13.5f, 3.5f, 13.8f, L"ice1.png");
        AddPlatform(PlatformType::Normal, -2.0f, 15.5f, 2.0f, 15.8f);

        // 式式 窕渠晦 式式
        AddPlatform(PlatformType::Normal, -1.5f, 17.5f, 1.5f, 17.8f);
    }

    // 式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式
    //  Ы概イ 儅撩 ⑦ぷ
    //
    //  texPath == nullptr ⊥ 欽儀 (顫殮滌 晦獄 儀鼻)
    //  texPath 雖薑 衛    ⊥ п渡 臢蝶籀 煎萄 (醞犒 煎萄 寞雖)
    //
    //  ≠ 億 PlatformType 蹺陛 衛 switch 縑 case 虜 蹺陛ж賊 腌棲棻.
    // 式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式
    void AddPlatform(PlatformType type,
        float lx, float by, float rx, float ty,
        const wchar_t* texPath = nullptr)
    {
        float w = rx - lx;
        float h = ty - by;
        float cx = (lx + rx) * 0.5f;
        float cy = (by + ty) * 0.5f;

        // 式式 顫殮滌 晦獄 儀鼻
        // ≠ 億 顫殮 蹺陛 衛 case 蹺陛
        XMFLOAT4 color = { 1, 1, 1, 1 };
        switch (type)
        {
        case PlatformType::Normal:      color = { 0.5f, 0.4f, 0.3f, 1.0f }; break;
        case PlatformType::Ice:         color = { 0.6f, 0.9f, 1.0f, 1.0f }; break;
        case PlatformType::PassThrough: color = { 0.4f, 0.8f, 0.4f, 1.0f }; break;
        }

        // 式式 該じ葬橡 儅撩 (Ы概イ葆棻 絮董)
        auto* mat = new ColorMaterial(DefaultShaders, color, &Gfx);
        OwnedMaterials.push_back(mat); // GameLoop 陛 模嶸掏 婦葬

        // 式式 臢蝶籀 (橈戲賊 欽儀 斜渠煎)
        auto* srv = TexCache.Get(texPath);
        if (srv) mat->SetTexture(srv);

        // 式式 GameObject 褻董
        auto* go = new GameObject(cx, cy);
        go->Scale = { w, h };
        go->AddComponent(new MeshRenderer(QuadMesh, mat));
        go->AddComponent(new PlatformComp(type));

        Platforms.push_back(go);
    }

    // 式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式
    void BuildPlayer()
    {
        auto* player = new GameObject(0.0f, 1.5f);
        player->Scale = { 0.6f, 0.6f };

        player->AddComponent(new MeshRenderer(QuadMesh, MatPlayer));

        auto* pc = new PlayerController();
        pc->Platforms = &Platforms;   // 瞪羲 渠褐 霜蕾 輿殮
        pc->Checkpoint = { 0.0f, 1.5f };
        player->AddComponent(pc);

        player->AddComponent(new JumpChargeBar(BarMesh, MatChargeBar));

        World.push_back(player);
    }

    // 式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式式
    void Run()
    {
        MSG msg = {};
        while (msg.message != WM_QUIT && Running)
        {
            if (PeekMessage(&msg, 0, 0, 0, PM_REMOVE))
            {
                TranslateMessage(&msg); DispatchMessage(&msg);
            }
            else { ProcessInput(); Update(); Render(); }
        }
    }

    void ProcessInput()
    {
        if (GetAsyncKeyState(VK_ESCAPE) & 0x8000) Running = false;

        bool lbNow = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
        if (lbNow && !MousePressed)
        {
            POINT pt; GetCursorPos(&pt); ScreenToClient(Win.hWnd, &pt);
            Vec2 w = Cam.ScreenToWorld((int)pt.x, (int)pt.y);
            printf("[Map] Click World: (%.3f, %.3f)\n", w.x, w.y);
        }
        MousePressed = lbNow;

        for (auto* go : World)     go->Input();
        for (auto* go : Platforms) go->Input();
    }

    void Update()
    {
        float dt = Timer.Get();
        for (auto* go : Platforms) go->Update(dt, &Gfx);
        for (auto* go : World)     go->Update(dt, &Gfx);
        if (!World.empty()) Cam.Follow(World[0]->Pos, dt);
    }

    void Render()
    {
        float bg[] = { 0.08f, 0.08f, 0.15f, 1.0f };
        Gfx.Context->ClearRenderTargetView(Gfx.RTV, bg);

        D3D11_VIEWPORT vp = { 0, 0, (float)Win.Width, (float)Win.Height, 0, 1 };
        Gfx.Context->RSSetViewports(1, &vp);
        Gfx.Context->OMSetRenderTargets(1, &Gfx.RTV, NULL);
        Gfx.Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        Cam.Upload(&Gfx);
        for (auto* go : Platforms) go->Render(&Gfx);
        for (auto* go : World)     go->Render(&Gfx);

        Gfx.SwapChain->Present(1, 0);
    }
};

// ============================================================
//  WndProc
// ============================================================
static GameLoop* g_Engine = nullptr;

LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m)
    {
    case WM_SIZE:
        if (g_Engine && g_Engine->Gfx.Context)
        {
            int nw = LOWORD(l), nh = HIWORD(l);
            if (nw > 0 && nh > 0)
            {
                g_Engine->Win.Width = nw;
                g_Engine->Win.Height = nh;
                g_Engine->Gfx.Context->OMSetRenderTargets(0, nullptr, nullptr);
                g_Engine->Gfx.RTV->Release(); g_Engine->Gfx.RTV = nullptr;
                g_Engine->Gfx.SwapChain->ResizeBuffers(0, nw, nh, DXGI_FORMAT_UNKNOWN, 0);
                g_Engine->Gfx.CreateRTV();
                g_Engine->Cam.ViewW = (float)nw;
                g_Engine->Cam.ViewH = (float)nh;
            }
        }
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    }
    return DefWindowProc(h, m, w, l);
}

// ============================================================
//  WinMain
// ============================================================
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int)
{
    GameLoop engine;
    g_Engine = &engine;

    if (!engine.Initialize(hInst, WndProc))
    {
        MessageBoxA(NULL, "Initialization failed!", "Error", MB_OK);
        return -1;
    }

    engine.Run();
    g_Engine = nullptr;
    return 0;
}
