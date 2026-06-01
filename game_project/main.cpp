/// ============================================================
//  JumpKing-Style Platformer  |  DirectX 11  |  Single File
//
//  ★ 확장 가이드 ★
//  ─────────────────────────────────────────────────────────
//  [새 아이템 추가]
//    1. ItemType 열거형에 항목 추가
//    2. RouletteState::Items[] 배열에 추가 (룰렛에서 등장)
//    3. RouletteState::ITEM_COUNT 값 +1
//    4. PlayerItemState 에 타이머/플래그 멤버 추가
//    5. PlayerItemState::Apply() switch 에 발동 로직 추가
//    6. PlayerItemState::Update() 에 타이머 처리 추가
//    7. GameLoop 에 아이콘 머티리얼 추가
//       (MatRouletteIcon[], MatItemBar[] 배열 크기도 함께 늘릴 것)
//
//  [랜덤박스 배치]
//    BuildRandomBoxes() 안에 한 줄 추가:
//    AddRandomBox(cx, cy);
//    AddRandomBox(cx, cy, L"tex.png");
//
//  [새 플랫폼 배치]
//    BuildMap() 안에 한 줄 추가:
//    AddPlatform(타입, LX, BY, RX, TY);            // 단색
//    AddPlatform(타입, LX, BY, RX, TY, L"tex.png"); // 텍스처
//
//  [새 플랫폼 타입 추가]
//    1. PlatformType 열거형에 항목 추가
//    2. AddPlatform() 의 switch 에 기본 색상 추가
//    3. PlayerController::UpdateMovement() 의
//       타입별 분기(2단계)에 이동 동작 추가
//    ※ 점프 중 이동 차단(1단계)은 자동 적용됨
//
//  [새 컴포넌트 추가]
//    Component 를 상속 → Start/Input/Update/Render 오버라이드
//    원하는 GameObject 에 AddComponent() 로 붙이면 끝
//
//  [텍스처]
//    실행파일 옆에 PNG 배치 후 AddPlatform 마지막 인자로 경로 전달
//    같은 파일은 TexCache 가 자동으로 중복 로드를 방지함
// ─────────────────────────────────────────────────────────
// 구현 내용 
// 2026.05.08
// 게임 루프 기본 구조, 카메라 추적
// 물리 : 중력, 가속도 기반 지면 이동(최고속도 클램프), 공중 관성 유지 + 약한 보정, 점프 충전 시스템(홀드 → 릴리즈), 점프 중 이동 차단(모든 타입 공통), 공중 재점프 방지, AABB 충돌 해결(최소 침투 방향 밀어냄)
// 플렛폼 : normal, ice, passThrough 
// 조작키 : 방향키(이동), space(점프), f(fly), c/r(checkpoint), esc(종료), 마우스 좌클릭(콘솔 좌표 출력)
// 추가 : 아이템(랜덤박스 → 룰렛 → 발동), hitSideWall 조건 수정, 공중 JumpCharge 즉시 초기화
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
#include <cstdlib>
#include <cmath>

#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(linker, "/entry:WinMainCRTStartup /subsystem:console")

using namespace DirectX;

// ============================================================
//  전방 선언
// ============================================================
class GraphicsContext;
class GameObject;
struct PlayerController;

// ============================================================
//  화면 상수
// ============================================================
static constexpr int SCREEN_W = 1280;
static constexpr int SCREEN_H = 720;

// ============================================================
//  물리 상수  ─  게임 느낌 조정은 여기서
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
static constexpr float GROUND_ACCEL = 10.0f;
static constexpr float GROUND_DECEL = 20.0f;

// ============================================================
//  아이템 상수
// ============================================================
static constexpr float ITEM_FLY_DURATION = 3.0f;
static constexpr float FLY_ITEM_SPEED = 2.0f;
static constexpr float ITEM_PASSTHROUGH_DURATION = 5.0f;
static constexpr float ITEM_SHIELD_DURATION = 8.0f;
static constexpr float ITEM_PICKUP_RADIUS = 0.5f;

// ============================================================
//  룰렛 상수
// ============================================================
static constexpr float ROULETTE_DURATION = 2.0f;
static constexpr float ROULETTE_INTERVAL_MIN = 0.05f;
static constexpr float ROULETTE_INTERVAL_MAX = 0.4f;

// ============================================================
//  기본 자료형
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
//  플랫폼 타입
//  ★ 새 타입 추가 시:
//     1. 여기에 열거값 추가
//     2. AddPlatform() switch 에 기본색 추가
//     3. UpdateMovement() 2단계 분기에 이동 동작 추가
// ============================================================
enum class PlatformType {
    Normal,
    Ice,
    PassThrough,
    Vanishing,
    Reverse,
    Moving,
    Bomb
};

// ============================================================
//  아이템 타입
//  ★ 새 아이템 추가 시 여기에 열거값 추가
// ============================================================
enum class ItemType { None = 0, Fly, PassThrough, Shield, Checkpoint };

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

        D3D11_SAMPLER_DESC smpDesc = {};
        smpDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
        smpDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
        smpDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
        smpDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
        Device->CreateSamplerState(&smpDesc, &Sampler);

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
//  텍스처 로더 (WIC)
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
        printf("[Texture] 파일 열기 실패: %ls\n", path);
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

    printf("[Texture] 로드 성공: %ls (%ux%u)\n", path, w, h);
    return srv;
}

// ============================================================
//  TextureCache
// ============================================================
class TextureCache
{
    GraphicsContext* gfx = nullptr;
    std::unordered_map<std::wstring, ID3D11ShaderResourceView*> cache;
public:
    void Init(GraphicsContext* g) { gfx = g; }

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
//  Material 기반
// ============================================================
class Material
{
public:
    ShaderSet Shaders;
    explicit Material(ShaderSet s) : Shaders(s) {}
    virtual ~Material() {}
    virtual void Bind(GraphicsContext* gfx) = 0;
};

// ── ColorMaterial : 단색 또는 텍스처 ──────────────────────
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
    bool  Visible = true;

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
        if (!Active || !Visible) return;
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
//  ─ 점프킹 스타일 카메라
//    [1] X축은 시작 위치에 고정 (좌우 카메라 이동 없음)
//    [2] Y축은 한 화면 단위로 컷 전환 (부드러운 추적 X)
//    [3] Y는 StartY 미만으로 절대 내려가지 않음 (월드 바닥 = 시작 화면)
// ============================================================
class Camera
{
public:
    Vec2          Pos = { 0, 0 };
    float         ViewW = 0;
    float         ViewH = 0;
    ID3D11Buffer* CB = nullptr;

    // ── [추가] 점프킹 카메라 제어용 멤버 ──
    float StartY = 0.0f;   // 카메라 Y 최솟값 (시작 화면 위치)
    float FixedX = 0.0f;   // 카메라 X 고정값 (점프킹은 좌우 이동 없음)

    void Init(GraphicsContext* gfx, int w, int h)
    {
        ViewW = (float)w; ViewH = (float)h;
        D3D11_BUFFER_DESC bd = {};
        bd.Usage = D3D11_USAGE_DEFAULT;
        bd.ByteWidth = sizeof(CbCamera);
        bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        gfx->Device->CreateBuffer(&bd, nullptr, &CB);
    }

    // ────────────────────────────────────────────────────────
    //  [점프킹 스타일 카메라 추적]
    //  ─ dt 는 이제 사용하지 않음 (부드러운 보간 제거)
    //    시그니처는 유지해서 호출부 변경 없게 함
    // ────────────────────────────────────────────────────────
    void Follow(Vec2 target, float dt)
    {
        // [1. X축 고정]
        //    점프킹은 좌우 카메라 이동이 없음. 시작 위치 그대로.
        Pos.x = FixedX;

        // [2. 한 화면 세로 크기 계산]
        //    셰이더 좌표 변환에서 보이는 세로 범위 = ViewH / 200.0f
        //    (창 크기가 800x600일 때 정확히 3.0 유닛)
        const float SCREEN_WORLD_H = ViewH / 200.0f;
        const float halfView = SCREEN_WORLD_H * 0.5f;

        // [3. 플레이어가 현재 화면 상단을 넘으면 한 화면 위로 컷]
        //    while 인 이유: 점프 한 번에 두 화면 이상 넘어가는 경우 대비
        while (target.y > Pos.y + halfView)
            Pos.y += SCREEN_WORLD_H;

        // [4. 플레이어가 현재 화면 하단을 넘으면 한 화면 아래로 컷]
        //    단, StartY 미만으로는 절대 내려가지 않음 (월드 바닥)
        while (target.y < Pos.y - halfView && Pos.y > StartY)
            Pos.y -= SCREEN_WORLD_H;

        // [5. 안전장치: 부동소수점 누적 오차 또는 StartY 침범 방지]
        if (Pos.y < StartY) Pos.y = StartY;
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
// ============================================================
class PlatformComp : public Component
{
public:
    PlatformType Type;
    
    // Moving 플랫폼 시작 위치 저장
    Vec2 StartPos = { 0, 0 };

    // 이동 시간 누적
    float MoveTimer = 0.0f;

    // 이동 범위
    float MoveRange = 0.6f;

    // 이동 속도
    float MoveSpeed = 1.5f;

    // 플랫폼 활성 상태
    bool IsActive = true;

    // Vanishing 전용 타이머
    float Timer = 0.0f;

    // 플레이어가 한번 밟았는지
    bool Triggered = false;

    // Bomb 플랫폼
    bool IsShaking = false;

    float BombTimer = 0.0f;

    float IdleTime = 2.0f;

    float ShakeTime = 0.7f;

    float OriginX = 0.0f;

    bool HasBouncedPlayer = false;

    PlatformComp(PlatformType t) : Type(t) {}

    void Start(GraphicsContext* gfx) override
    {
      // Moving 플랫폼용
      StartPos = Owner->Pos;

      // Bomb 플랫폼 원래 위치 저장
      OriginX = Owner->Pos.x;
    }

    void Update(float dt) override
    {
        if (Type == PlatformType::Vanishing)
        {
            if (Triggered)
            {
                Timer += dt;
                if (Timer >= 3.0f && IsActive)
                {
                    IsActive = false;
                    Owner->Visible = false;
                }
                if (Timer >= 5.0f)
                {
                    IsActive = true;
                    Owner->Visible = true;
                    Triggered = false;
                    Timer = 0.0f;
                }
            }
        }

        if (Type == PlatformType::Moving)
        {
            MoveTimer += dt * MoveSpeed;
            Owner->Pos.x = StartPos.x + sinf(MoveTimer) * MoveRange;
        }

        if (Type == PlatformType::Bomb)
        {
          BombTimer += dt;

          if (!IsShaking && BombTimer >= IdleTime)
          {
            IsShaking = true;
            HasBouncedPlayer = false;
          }

          if (IsShaking)
          {
            Owner->Pos.x =
              OriginX + sinf(BombTimer * 50.0f) * 0.08f;

            if (BombTimer >= IdleTime + ShakeTime)
            {
              IsShaking = false;

              BombTimer = 0.0f;

              Owner->Pos.x = OriginX;
            }
          }
        }
    }

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
//  아이템 효과 상태  (PlayerController 전방 선언 필요)
// ============================================================
struct PlayerItemState
{
    bool  FlyActive = false;
    float FlyTimer = 0.0f;

    bool  PassThroughActive = false;
    float PassThroughTimer = 0.0f;

    bool  ShieldActive = false;
    float ShieldTimer = 0.0f;

    bool  CheckpointSet = false;
    Vec2  CheckpointPos = { 0, 0 };

    void Apply(ItemType type, PlayerController* pc);
    void Update(float dt, PlayerController* pc);
};

// ============================================================
//  룰렛 상태
//  ★ 아이템 추가 시 Items[], ITEM_COUNT 수정
// ============================================================
struct RouletteState
{
    static constexpr int ITEM_COUNT = 4;
    ItemType Items[ITEM_COUNT] = {
        ItemType::Fly,
        ItemType::PassThrough,
        ItemType::Shield,
        ItemType::Checkpoint
    };

    bool  Active = false;
    float Timer = 0.0f;
    float FlipTimer = 0.0f;
    int   CurrentSlot = 0;
    int   ResultSlot = -1;
    bool  ShowResult = false;
    float ShowResultTimer = 0.0f;
    static constexpr float SHOW_RESULT_DURATION = 1.0f;

    bool IsRunning() const { return Active || ShowResult; }

    void Start()
    {
        Active = true;
        Timer = ROULETTE_DURATION;
        FlipTimer = ROULETTE_INTERVAL_MIN;
        CurrentSlot = rand() % ITEM_COUNT;
        ResultSlot = rand() % ITEM_COUNT;
        ShowResult = false;
        ShowResultTimer = 0.0f;
    }

    bool Update(float dt)
    {
        if (!Active)
        {
            if (ShowResult)
            {
                ShowResultTimer -= dt;
                if (ShowResultTimer <= 0.0f)
                {
                    ShowResult = false; ShowResultTimer = 0.0f; return true;
                }
            }
            return false;
        }
        Timer -= dt; FlipTimer -= dt;
        if (FlipTimer <= 0.0f)
        {
            CurrentSlot = (CurrentSlot + 1) % ITEM_COUNT;
            float prog = 1.0f - max(0.0f, Timer / ROULETTE_DURATION);
            FlipTimer = ROULETTE_INTERVAL_MIN
                + (ROULETTE_INTERVAL_MAX - ROULETTE_INTERVAL_MIN) * prog;
        }
        if (Timer <= 0.0f)
        {
            CurrentSlot = ResultSlot;
            Active = false;
            ShowResult = true;
            ShowResultTimer = SHOW_RESULT_DURATION;
        }
        return false;
    }

    ItemType GetCurrent() const { return Items[CurrentSlot]; }
    ItemType GetResult()  const { return Items[ResultSlot]; }
    float ShowResultScale() const
    {
        float t = 1.0f - ShowResultTimer / SHOW_RESULT_DURATION;
        return 1.0f + 0.5f * sinf(t * 3.14159f);
    }
};

// ============================================================
//  PlayerController
// ============================================================

/*
추가된 로직
1. 점프 시 지면 모서리에 부딪히는 경우 튕겨져 나오는 로직
2. 캐릭터 점프 시 캐릭터가 회전 후 바닥에 착지 시 회전률 초기화로 정상적이게 보이게끔 설정
3. 코요테 타임 추가 -> 캐릭터가 허공에 떠도 0.1초 간 점프를 허용하여 억울하게 추락하는 것을 방지
4. 공중에서 캐릭터의 위치 제어 기능 (물리 엔진)
5. 점프 버퍼 -> 착지 직전 0.15초 내에 입력된 점프 명령을 입력하고 착지 즉시 점프가 가능하게끔 구현 
    << 이 부분은 차징 점프여서 굳이 없어도 되지 않을까 싶습니다!
*/

class PlayerController : public Component
{
public:
    // ── 외부 주입 ──
    std::vector<GameObject*>* Platforms = nullptr;
    PlayerItemState* ItemState = nullptr;
    GameObject* CheckpointFlag = nullptr;

    // ── 물리 상태 ──
    Vec2  Vel = { 0, 0 };
    bool  OnGround = false;
    float HalfW = 0.3f;
    float HalfH = 0.3f;

    Vec2 PrevPos = { 0, 0 };

    // ── 점프 상태 (충전 시스템) ──
    bool  PrevSpace = false;
    bool  SpacePressedThisFrame = false;
    bool  SpaceHeld = false;
    float JumpCharge = 0.0f;
    bool  JumpedThisPress = false;

    // ── 고급 조작감 타이머 ──
    float CoyoteTimer = 0.0f;
    float JumpBufferTimer = 0.0f;
    float CurrentShakeX = 0.0f;

    // ── 점프킹 특화 상태 ──
    bool  IsStunned = false;

    // ── 입력 ──
    float MoveInput = 0.0f;

    // ── 지면 타입 플래그 ──
    bool  OnIce = false;
    float ReverseTimer = 0.0f;
    bool IsReverse() const { return ReverseTimer > 0.0f; }

    // ── 튕김(Bounce) 설정 ──
    float BounceFactor = 0.85f;
    float MinBounceSpeed = 3.0f;
    float MaxBounceSpeed = 6.0f;

    // ── 비행 모드 ──
    bool FlyMode = false;   // DevFly (F키)
    bool item_flymode = false;   // 아이템 Fly

    // ── 체크포인트 ──
    Vec2  Checkpoint = { 0, 0.5f };

    // ── 룰렛 ──
    RouletteState Roulette;

    // ── 플레이어 상태별 머티리얼 (외부 주입) ──
    ColorMaterial* MatNormal = nullptr;
    ColorMaterial* MatFly = nullptr;
    ColorMaterial* MatPassThrough = nullptr;
    ColorMaterial* MatShield = nullptr;

    // ── 입력 엣지 감지 ──
    bool PrevF = false, PrevR = false;

    // ────────────────────────────────────────────────────────
    void Input() override
    {
        bool spaceNow = (GetAsyncKeyState(VK_SPACE) & 0x8000) != 0;
        SpacePressedThisFrame = spaceNow && !PrevSpace;
        SpaceHeld = spaceNow;
        PrevSpace = spaceNow;

        bool fNow = (GetAsyncKeyState('F') & 0x8000) != 0;
        bool rNow = (GetAsyncKeyState('R') & 0x8000) != 0;

        if (GetAsyncKeyState('1') & 0x8000) ItemState->Apply(ItemType::Fly, this);
        if (GetAsyncKeyState('2') & 0x8000) ItemState->Apply(ItemType::PassThrough, this);
        if (GetAsyncKeyState('3') & 0x8000) ItemState->Apply(ItemType::Shield, this);
        if (GetAsyncKeyState('4') & 0x8000) ItemState->Apply(ItemType::Checkpoint, this);



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
            if (CheckpointFlag->Active == false) return;
            if (CheckpointFlag) CheckpointFlag->Active = false;
            Owner->Pos = Checkpoint; Vel = { 0, 0 };
            printf("[Dev] Reset (%.2f, %.2f)\n", Checkpoint.x, Checkpoint.y);
        }


        PrevF = fNow; PrevR = rNow;
    }

    // ────────────────────────────────────────────────────────
    void Update(float dt) override
    {
        // ── 아이템 효과 갱신 ──
        if (ItemState) ItemState->Update(dt, this);

        // ── 룰렛 갱신 ──
        if (Roulette.IsRunning())
        {
            bool done = Roulette.Update(dt);
            if (done && ItemState)
            {
                ItemType result = Roulette.GetResult();
                printf("[RandomBox] 확정: type=%d → 즉시 발동\n", (int)result);
                ItemState->Apply(result, this);
            }
        }

        // ── 머티리얼 전환 ──
        if (MatNormal)
        {
            auto* mr = Owner->GetComponent<MeshRenderer>();
            if (mr)
            {
                if (ItemState && ItemState->FlyActive)         mr->pMat = MatFly;
                else if (ItemState && ItemState->PassThroughActive) mr->pMat = MatPassThrough;
                else if (ItemState && ItemState->ShieldActive)      mr->pMat = MatShield;
                else                                                 mr->pMat = MatNormal;
            }
        }

        PrevPos = Owner->Pos;

        // ── Fly 아이템 발동 중이면 FlyMode 강제 ON ──
        if (ItemState && ItemState->FlyActive) FlyMode = true;

        if (FlyMode)
        {
            UpdateFly(dt);
            if (item_flymode) ResolveAllCollisions(dt);
            Owner->Rot += (0.0f - Owner->Rot) * 15.0f * dt;
            return;
        }

        Vel.y += GRAVITY * dt;

        if (ReverseTimer > 0.0f) ReverseTimer -= dt;

        UpdateMovement(dt);
        UpdateJump(dt);


        // 순수 물리 이동만 적용
        Owner->Pos.x += Vel.x * dt;
        Owner->Pos.y += Vel.y * dt;

        ResolveAllCollisions(dt);

        if (OnGround && JumpCharge > 0.05f)
        {
            float shakeIntensity = (JumpCharge * JumpCharge) * 0.25f;
            float timeFactor = sinf(dt * 1000.0f + JumpCharge * 50.0f);
            CurrentShakeX = (timeFactor > 0.0f ? 1.0f : -1.0f) * shakeIntensity;
        }
        else
        {
            CurrentShakeX = 0.0f;
        }

        if (!OnGround) Owner->Rot -= Vel.x * 3.0f * dt;
        else           Owner->Rot += (0.0f - Owner->Rot) * 15.0f * dt;
    }

private:
    // ── 수평 이동 ────────────────────────────────────────────
    void UpdateMovement(float dt)
    {
        if (SpaceHeld && OnGround && !IsStunned)
        {
            if (OnIce) Vel.x *= (1.0f - ICE_DRAG * dt);
            else       Vel.x += (0.0f - Vel.x) * BRAKE_ACCEL * dt;
            return;
        }

        if (OnGround)
        {
            if (OnIce)
            {
                if (MoveInput != 0.0f && !IsStunned)
                {
                    Vel.x += MoveInput * ICE_ACCEL * dt;
                    Vel.x = max(-MOVE_SPEED, min(MOVE_SPEED, Vel.x));
                }
                else Vel.x *= (1.0f - ICE_DRAG * dt);
            }
            else if (IsReverse())
            {
                float reverseInput = -MoveInput;
                if (reverseInput != 0.0f && !IsStunned)
                {
                    Vel.x += reverseInput * GROUND_ACCEL * dt;
                    Vel.x = max(-MOVE_SPEED, min(MOVE_SPEED, Vel.x));
                }
                else
                {
                    float decel = GROUND_DECEL * dt;
                    if (Vel.x > 0) Vel.x = max(0.0f, Vel.x - decel);
                    else           Vel.x = min(0.0f, Vel.x + decel);
                }
            }
            else
            {
                if (MoveInput != 0.0f && !IsStunned)
                {
                    Vel.x += MoveInput * GROUND_ACCEL * dt;
                    Vel.x = max(-MOVE_SPEED, min(MOVE_SPEED, Vel.x));
                }
                else
                {
                    float decel = GROUND_DECEL * dt;
                    if (Vel.x > 0) Vel.x = max(0.0f, Vel.x - decel);
                    else           Vel.x = min(0.0f, Vel.x + decel);
                }
            }
        }
        else
        {
            if (IsStunned) return;

            float airAccel = 12.0f;
            float airDecel = 2.0f;

            float input = IsReverse() ? -MoveInput : MoveInput;

            if (input != 0.0f)
            {
                Vel.x += input * airAccel * dt;
                Vel.x = max(-MOVE_SPEED, min(MOVE_SPEED, Vel.x));
            }
            else
            {
                if (Vel.x > 0) Vel.x = max(0.0f, Vel.x - airDecel * dt);
                else           Vel.x = min(0.0f, Vel.x + airDecel * dt);
            }
        }
    }

    // ── 점프 (코요테 타임 & 버퍼링 적용) ────────────────────
    void UpdateJump(float dt)
    {
        if (OnGround) CoyoteTimer = 0.1f;
        else if (CoyoteTimer > 0.0f) CoyoteTimer -= dt;

        if (SpacePressedThisFrame) JumpBufferTimer = 0.15f;
        else if (JumpBufferTimer > 0.0f) JumpBufferTimer -= dt;

        bool canJump = (OnGround || CoyoteTimer > 0.0f) && !IsStunned;

        if (canJump)
        {
            if ((SpaceHeld || JumpBufferTimer > 0.0f) && !JumpedThisPress)
                JumpCharge = min(JumpCharge + JUMP_CHARGE * dt, 1.0f);

            if (!SpaceHeld && JumpCharge > 0.0f)
            {
                float speed = JUMP_MIN + (JUMP_MAX - JUMP_MIN) * JumpCharge;
                Vel.y = speed;

                float jumpInput = IsReverse() ? -MoveInput : MoveInput;
                Vel.x = jumpInput * MOVE_SPEED;

                JumpCharge = 0.0f;
                JumpedThisPress = true;
                OnGround = false;

                CoyoteTimer = 0.0f;
                JumpBufferTimer = 0.0f;
            }
        }
        else
        {
            // ★ 수정: 공중으로 떨어지는 순간 차징값 즉시 초기화
            JumpCharge = 0.0f;
            if (!SpaceHeld) JumpedThisPress = false;
        }
    }

    // ── 충돌 해결 ────────────────────────────────────────────
    void ResolveAllCollisions(float dt)
    {
        OnGround = false;
        OnIce = false;
        
        if (!Platforms) return;
        for (auto* go : *Platforms)
        {
            auto* plat = go->GetComponent<PlatformComp>();
            if (plat) ResolveCollision(plat, dt);
        }
    }

    void ResolveCollision(PlatformComp* plat, float dt)
    {
        AABB player = GetAABB();
        AABB platform = plat->GetAABB();

        if (!plat->IsActive) return;
        if (!player.Overlaps(platform)) return;

        bool shielded = (ItemState && ItemState->ShieldActive);

        float oL = player.right - platform.left;
        float oR = platform.right - player.left;
        float oB = player.top - platform.bottom;
        float oT = platform.top - player.bottom;
        float minO = min(min(oL, oR), min(oB, oT));
        float prevBottom = PrevPos.y - HalfH;

        bool fromAbove = (prevBottom >= platform.top - 0.01f);

        // ── PassThrough 플랫폼 타입 OR 아이템 PassThrough ──
        if (plat->Type == PlatformType::PassThrough ||
            (ItemState && ItemState->PassThroughActive))
        {
            if (minO == oT && Vel.y <= 0.0f && fromAbove)
            {
                Owner->Pos.y = platform.top + HalfH;
                Vel.y = 0;
                OnGround = true;
            }


            if (ItemState && ItemState->PassThroughActive) {
                if (shielded) return;

                if (plat->Type == PlatformType::Ice) OnIce = true;

                if (plat->Type == PlatformType::Reverse)
                    ReverseTimer = 2.0f;
                else
                    ReverseTimer = 0.0f;

                if (plat->Type == PlatformType::Vanishing)
                    plat->Triggered = true;
            }
            return;
        }




        const float HORIZONTAL_BOUNCE = 1.3f;

        // 수정: minO != oB 조건 추가
        bool hitSideWall = (minO == oL || minO == oR) ||
            (abs(Vel.x) > 1.0f && minO != oT && minO != oB);

        // --- ResolveCollision 함수 내부의 벽 충돌 분기문 수정 ---

        if (hitSideWall && !OnGround)
        {
            if (oL < oR) // 왼쪽 벽 옆구리 박음 (오른쪽 이동 중 -> 왼쪽으로 튕김)
            {
                Owner->Pos.x = platform.left - HalfW;
                if (Vel.x >= 0.0f)
                {
                    float speed = max(MinBounceSpeed, Vel.x);
                    // 1. 우선 원래 설계대로 1.3배 증폭 계산
                    float bouncedSpeed = speed * HORIZONTAL_BOUNCE;

                    // 2. ★ 최대 증폭값 클램프 (MaxBounceSpeed를 넘지 않도록 제한)
                    if (bouncedSpeed > MaxBounceSpeed) bouncedSpeed = MaxBounceSpeed;

                    // 3. 최종 속도 적용 (왼쪽 방향이므로 마이너스)
                    Vel.x = -bouncedSpeed;

                    if (Vel.y > 0.0f) Vel.y *= 0.6f;
                }
            }
            else // 오른쪽 벽 옆구리 박음 (왼쪽 이동 중 -> 오른쪽으로 튕김)
            {
                Owner->Pos.x = platform.right + HalfW;
                if (Vel.x <= 0.0f)
                {
                    float speed = max(MinBounceSpeed, -Vel.x);
                    // 1. 우선 원래 설계대로 1.3배 증폭 계산
                    float bouncedSpeed = speed * HORIZONTAL_BOUNCE;

                    // 2. ★ 최대 증폭값 클램프
                    if (bouncedSpeed > MaxBounceSpeed) bouncedSpeed = MaxBounceSpeed;

                    // 3. 최종 속도 적용 (오른쪽 방향이므로 플러스)
                    Vel.x = bouncedSpeed;

                    if (Vel.y > 0.0f) Vel.y *= 0.6f;
                }
            }
        }
        else
        {
            if (minO == oT)
            {
                if (Vel.y <= 0.0f)
                {
                    Owner->Pos.y = platform.top + HalfH;
                    Vel.y = 0;
                    OnGround = true;

                    if (shielded) return;

                    if (plat->Type == PlatformType::Ice) OnIce = true;

                    if (plat->Type == PlatformType::Reverse)
                        ReverseTimer = 2.0f;
                    else
                        ReverseTimer = 0.0f;

                    if (plat->Type == PlatformType::Vanishing)
                        plat->Triggered = true;

                    if (plat->Type == PlatformType::Bomb && plat->IsShaking && !plat->HasBouncedPlayer)
                    {
                      plat->HasBouncedPlayer = true;

                      Vel.y = 12.0f;

                      float dir =
                        (rand() % 2 == 0) ? -1.0f : 1.0f;

                      Vel.x = dir * 6.0f;

                      OnGround = false;
                    }
                }
            }
            else if (minO == oB)
            {
                Owner->Pos.y = platform.bottom - HalfH;
                if (Vel.y > 0.0f) Vel.y = -Vel.y * BounceFactor;
            }
        }
    }

    void UpdateFly(float dt)
    {
        Vec2 dir = { 0, 0 };
        if (GetAsyncKeyState(VK_LEFT) & 0x8000) dir.x -= 1.0f;
        if (GetAsyncKeyState(VK_RIGHT) & 0x8000) dir.x += 1.0f;
        if (GetAsyncKeyState(VK_UP) & 0x8000) dir.y += 1.0f;
        if (GetAsyncKeyState(VK_DOWN) & 0x8000) dir.y -= 1.0f;


        if (item_flymode) {
            Owner->Pos.x += dir.x * FLY_ITEM_SPEED * dt;
            Owner->Pos.y += dir.y * FLY_ITEM_SPEED * dt;
        }
        else {
            Owner->Pos.x += dir.x * FLY_SPEED * dt;
            Owner->Pos.y += dir.y * FLY_SPEED * dt;
        }


        Vel = { 0, 0 };
        IsStunned = false;
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
//  PlayerItemState 구현 (PlayerController 정의 이후)
// ============================================================
inline void PlayerItemState::Apply(ItemType type, PlayerController* pc)
{
    switch (type)
    {
    case ItemType::Fly:
        pc->JumpCharge = 0.0f;
        FlyActive = true;
        FlyTimer = ITEM_FLY_DURATION;
        pc->FlyMode = true;
        pc->Vel = { 0, 0 };
        pc->item_flymode = true;
        printf("[Item] Fly 발동 %.1fs\n", ITEM_FLY_DURATION);
        break;

    case ItemType::PassThrough:
        PassThroughActive = true;
        PassThroughTimer = ITEM_PASSTHROUGH_DURATION;
        printf("[Item] PassThrough 발동 %.1fs\n", ITEM_PASSTHROUGH_DURATION);
        break;

    case ItemType::Shield:
        ShieldActive = true;
        ShieldTimer = ITEM_SHIELD_DURATION;
        printf("[Item] Shield 발동 %.1fs\n", ITEM_SHIELD_DURATION);
        break;

    case ItemType::Checkpoint:
        CheckpointSet = true;
        CheckpointPos = pc->Owner->Pos;
        pc->Checkpoint = pc->Owner->Pos;
        if (pc->CheckpointFlag)
        {
            pc->CheckpointFlag->Active = true;
            pc->CheckpointFlag->Pos = { pc->Owner->Pos.x, pc->Owner->Pos.y - 0.1f };
        }
        printf("[Item] Checkpoint 설치 (%.2f, %.2f)\n", CheckpointPos.x, CheckpointPos.y);
        break;

    default: break;
    }
}

inline void PlayerItemState::Update(float dt, PlayerController* pc)
{
    if (FlyActive)
    {
        FlyTimer -= dt;
        if (FlyTimer <= 0.0f)
        {
            FlyActive = false;
            FlyTimer = 0.0f;
            pc->FlyMode = false;
            pc->Vel = { 0, 0 };
            pc->item_flymode = false;
            printf("[Item] Fly 종료\n");
        }
    }
    if (PassThroughActive)
    {
        PassThroughTimer -= dt;
        if (PassThroughTimer <= 0.0f)
        {
            PassThroughActive = false;
            PassThroughTimer = 0.0f;
            printf("[Item] PassThrough 종료\n");
        }
    }
    if (ShieldActive)
    {
        ShieldTimer -= dt;
        if (ShieldTimer <= 0.0f)
        {
            ShieldActive = false;
            ShieldTimer = 0.0f;
            printf("[Item] Shield 종료\n");
        }
    }
}

// ============================================================
//  JumpChargeBar  ─  충전량 시각화
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
//  ActiveItemBar  ─  발동 중 아이템 잔여시간 바 (플레이어 위)
// ============================================================
class ActiveItemBar : public Component
{
    Mesh* pMesh = nullptr;
    ID3D11Buffer* CB = nullptr;
public:
    static constexpr int BAR_COUNT = 4;
    ColorMaterial* MatBars[BAR_COUNT] = {};

    ActiveItemBar(Mesh* m) : pMesh(m) {}

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
        if (!pc || !pc->ItemState || !CB) return;
        auto* is = pc->ItemState;

        float startY = Owner->Pos.y + 0.55f;
        int   row = 0;

        if (is->FlyActive && MatBars[0])
        {
            DrawBar(gfx, MatBars[0], Owner->Pos.x, startY + row * 0.10f,
                is->FlyTimer / ITEM_FLY_DURATION); ++row;
        }
        if (is->PassThroughActive && MatBars[1])
        {
            DrawBar(gfx, MatBars[1], Owner->Pos.x, startY + row * 0.10f,
                is->PassThroughTimer / ITEM_PASSTHROUGH_DURATION); ++row;
        }
        if (is->ShieldActive && MatBars[2])
        {
            DrawBar(gfx, MatBars[2], Owner->Pos.x, startY + row * 0.10f,
                is->ShieldTimer / ITEM_SHIELD_DURATION); ++row;
        }
    }

private:
    void DrawBar(GraphicsContext* gfx, ColorMaterial* mat,
        float cx, float cy, float ratio)
    {
        if (ratio <= 0.0f) return;
        float w = 0.6f * ratio;
        mat->Bind(gfx);
        XMMATRIX world = XMMatrixScaling(w, 0.06f, 1.0f)
            * XMMatrixTranslation(cx - (0.6f - w) * 0.5f, cy, 0.0f);
        CbWorld cb = { XMMatrixTranspose(world) };
        gfx->Context->UpdateSubresource(CB, 0, nullptr, &cb, 0, 0);
        gfx->Context->VSSetConstantBuffers(0, 1, &CB);
        UINT stride = sizeof(Vertex), offset = 0;
        gfx->Context->IASetVertexBuffers(0, 1, &pMesh->VB, &stride, &offset);
        gfx->Context->Draw(pMesh->Count, 0);
    }

    ~ActiveItemBar() override { if (CB) CB->Release(); }
};

// ============================================================
//  RouletteUI  ─  룰렛 아이콘 (플레이어 머리 위)
// ============================================================
class RouletteUI : public Component
{
public:
    ColorMaterial* MatIcon[RouletteState::ITEM_COUNT] = {};
    Mesh* pQuad = nullptr;
    ID3D11Buffer* CB = nullptr;
    float          IconSize = 0.5f;
    float          OffsetY = 0.7f;

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
        if (!pc) return;
        if (!pc->Roulette.Active && !pc->Roulette.ShowResult) return;

        int   idx = pc->Roulette.CurrentSlot;
        float scl = pc->Roulette.ShowResult ? pc->Roulette.ShowResultScale() : 1.0f;
        auto* mat = (idx >= 0 && idx < RouletteState::ITEM_COUNT) ? MatIcon[idx] : nullptr;
        if (!mat || !pQuad || !CB) return;

        mat->Bind(gfx);
        float size = IconSize * scl;
        XMMATRIX world = XMMatrixScaling(size, size, 1.0f)
            * XMMatrixTranslation(Owner->Pos.x,
                Owner->Pos.y + Owner->Scale.y * 0.5f + OffsetY, 0.0f);
        CbWorld cb = { XMMatrixTranspose(world) };
        gfx->Context->UpdateSubresource(CB, 0, nullptr, &cb, 0, 0);
        gfx->Context->VSSetConstantBuffers(0, 1, &CB);
        UINT stride = sizeof(Vertex), offset = 0;
        gfx->Context->IASetVertexBuffers(0, 1, &pQuad->VB, &stride, &offset);
        gfx->Context->Draw(pQuad->Count, 0);
    }

    ~RouletteUI() override { if (CB) CB->Release(); }
};

// ============================================================
//  CheckpointFlagRenderer  ─  깃대 + 깃발 렌더링
// ============================================================
class CheckpointFlagRenderer : public Component
{
    ID3D11Buffer* CB = nullptr;
public:
    Mesh* MastMesh = nullptr;
    Mesh* FlagMesh = nullptr;
    ColorMaterial* MatMast = nullptr;
    ColorMaterial* MatFlag = nullptr;

    void Start(GraphicsContext* gfx) override
    {
        D3D11_BUFFER_DESC bd = {};
        bd.Usage = D3D11_USAGE_DEFAULT;
        bd.ByteWidth = sizeof(CbWorld);
        bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        gfx->Device->CreateBuffer(&bd, nullptr, &CB);
    }

    void DrawQuad(GraphicsContext* gfx, ColorMaterial* mat, Mesh* mesh,
        float cx, float cy, float w, float h, float rot = 0.0f)
    {
        if (!mat || !mesh || !CB) return;
        mat->Bind(gfx);
        XMMATRIX world = XMMatrixScaling(w, h, 1.0f)
            * XMMatrixRotationZ(rot)
            * XMMatrixTranslation(cx, cy, 0.0f);
        CbWorld cb = { XMMatrixTranspose(world) };
        gfx->Context->UpdateSubresource(CB, 0, nullptr, &cb, 0, 0);
        gfx->Context->VSSetConstantBuffers(0, 1, &CB);
        UINT stride = sizeof(Vertex), offset = 0;
        gfx->Context->IASetVertexBuffers(0, 1, &mesh->VB, &stride, &offset);
        gfx->Context->Draw(mesh->Count, 0);
    }

    void Render(GraphicsContext* gfx) override
    {
        DrawQuad(gfx, MatMast, MastMesh, Owner->Pos.x, Owner->Pos.y, 0.06f, 0.5f);
        DrawQuad(gfx, MatFlag, FlagMesh, Owner->Pos.x + 0.15f, Owner->Pos.y + 0.1f, 0.3f, 0.22f);
    }

    ~CheckpointFlagRenderer() override { if (CB) CB->Release(); }
};

// ============================================================
//  RandomBoxComp  ─  접촉 시 사라지고 룰렛 시작
// ============================================================
class RandomBoxComp : public Component
{
public:
    bool              Picked = false;
    GameObject* Player = nullptr;
    PlayerController* PC = nullptr;

    float RespawnTimer = 0.0f;
    static constexpr float RESPAWN_DURATION = 30.0f;

    void Update(float dt) override
    {
        if (Picked)
        {
            RespawnTimer += dt;
            if (RespawnTimer >= RESPAWN_DURATION)
            {
                Picked = false;
                RespawnTimer = 0.0f;
                Owner->Visible = true;
                printf("[RandomBox] 재생성!\n");
            }
            return;
        }

        if (!Player || !PC) return;
        float dx = Owner->Pos.x - Player->Pos.x;
        float dy = Owner->Pos.y - Player->Pos.y;
        if (sqrtf(dx * dx + dy * dy) < ITEM_PICKUP_RADIUS&& !PC->Roulette.Active && !PC->Roulette.ShowResult)
        {
            Picked = true;
            RespawnTimer = 0.0f;   // 타이머 초기화
            Owner->Visible = false;
            PC->Roulette.Start();
            printf("[RandomBox] 룰렛 시작!\n");
        }
    }
};

// ============================================================
//  GoalComp  ─  클리어 "판정" (독립 시스템)
//   · 골 위치는 BuildGoal 에서 좌표 데이터로만 지정
//   · 플레이어가 골 영역에 닿으면 *Cleared 를 한 번만 true 로
//   · 맵을 확장해도 이 로직은 건드릴 필요 없음 (위치만 옮기면 됨)
// ============================================================
class GoalComp : public Component
{
public:
    GameObject* Player = nullptr;   // 주입
    bool* Cleared = nullptr;   // 주입 (GameLoop 소유)

    AABB GetAABB() const
    {
        float hw = Owner->Scale.x * 0.5f;
        float hh = Owner->Scale.y * 0.5f;
        return { Owner->Pos.x - hw, Owner->Pos.x + hw,
                 Owner->Pos.y - hh, Owner->Pos.y + hh };
    }

    void Update(float dt) override
    {
        if (!Player || !Cleared || *Cleared) return;   // 이미 클리어면 무시

        auto* pc = Player->GetComponent<PlayerController>();
        float phw = pc ? pc->HalfW : Player->Scale.x * 0.5f;
        float phh = pc ? pc->HalfH : Player->Scale.y * 0.5f;
        AABB p = { Player->Pos.x - phw, Player->Pos.x + phw,
                   Player->Pos.y - phh, Player->Pos.y + phh };

        if (p.Overlaps(GetAABB()))
        {
            *Cleared = true;
            printf("[Game] >>> MISSION COMPLETE <<<\n");
        }
    }
};

// ============================================================
//  MissionBanner  ─  클리어 "표시" (화면 정중앙 배너)
//   · Cleared 가 true 일 때만 mission_complete.png 를 카메라 중앙에 그림
//   · 카메라 위치에 그리므로 항상 화면 한가운데에 뜸
// ============================================================
class MissionBanner : public Component
{
    ID3D11Buffer* CB = nullptr;
public:
    Camera* Cam = nullptr;   // 주입
    bool* Cleared = nullptr;   // 주입
    ColorMaterial* Mat = nullptr;   // MISSION COMPLETE 텍스처
    Mesh* Quad = nullptr;
    float          Width = 10.0f;      // 배너 가로(월드 단위)
    float          Height = 2.5f;     // 배너 세로  (4:1 비율)

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
        if (!Cleared || !*Cleared || !Cam || !Mat || !Quad || !CB) return;

        Mat->Bind(gfx);
        // 화면 정중앙 = 카메라 위치 (셰이더가 camOffset 기준으로 좌표 변환하므로)
        XMMATRIX world = XMMatrixScaling(Width, Height, 1.0f)
            * XMMatrixTranslation(Cam->Pos.x, Cam->Pos.y, 0.0f);
        CbWorld cb = { XMMatrixTranspose(world) };
        gfx->Context->UpdateSubresource(CB, 0, nullptr, &cb, 0, 0);
        gfx->Context->VSSetConstantBuffers(0, 1, &CB);
        UINT stride = sizeof(Vertex), offset = 0;
        gfx->Context->IASetVertexBuffers(0, 1, &Quad->VB, &stride, &offset);
        gfx->Context->Draw(Quad->Count, 0);
    }

    ~MissionBanner() override { if (CB) CB->Release(); }
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
    bool Cleared = false;

    std::vector<GameObject*> World;
    std::vector<GameObject*> Platforms;
    std::vector<GameObject*> RandomBoxes;

    GameObject* FlagObject = nullptr;
    PlayerItemState  ItemStateData;

    // ── 공유 GPU 리소스 ──
    ShaderSet      DefaultShaders;
    Mesh* QuadMesh = nullptr;
    Mesh* BarMesh = nullptr;

    // ── 플레이어 머티리얼 ──
    ColorMaterial* MatPlayer = nullptr;
    ColorMaterial* MatPlayerFly = nullptr;
    ColorMaterial* MatPlayerPT = nullptr;
    ColorMaterial* MatPlayerShld = nullptr;
    ColorMaterial* MatChargeBar = nullptr;

    // ── 아이템 효과 바 / 룰렛 아이콘 머티리얼 ──
    static constexpr int ITEM_MAT_COUNT = 4;
    ColorMaterial* MatItemBar[ITEM_MAT_COUNT] = {};
    ColorMaterial* MatRouletteIcon[ITEM_MAT_COUNT] = {};

    // ── 깃발 머티리얼 ──
    ColorMaterial* MatFlagMast = nullptr;
    ColorMaterial* MatFlagFlag = nullptr;

    // ── 플랫폼 머티리얼 목록 (GameLoop 소유) ──
    std::vector<Material*> OwnedMaterials;

    bool MousePressed = false;

    // 패턴 플랫폼
    std::vector<PlatformComp*> PatternPlatforms;

    float PatternTimer = 0.0f;
    float PatternInterval = 1.5f;

    int PatternIndex = 0;

    GameLoop() { printf("[Engine] GameLoop Created.\n"); }

    ~GameLoop()
    {
        for (auto* go : World)       delete go;
        for (auto* go : Platforms)   delete go;
        for (auto* go : RandomBoxes) delete go;
        if (FlagObject) delete FlagObject;
        for (auto* m : OwnedMaterials) delete m;
        for (int i = 0; i < ITEM_MAT_COUNT; ++i)
        {
            delete MatItemBar[i];
            delete MatRouletteIcon[i];
        }
        delete MatFlagMast; delete MatFlagFlag;
        CoUninitialize();
        delete MatPlayer;    delete MatPlayerFly;
        delete MatPlayerPT;  delete MatPlayerShld;
        delete MatChargeBar;
        delete QuadMesh;     delete BarMesh;
        DefaultShaders.Release();
        printf("[Engine] All resources released.\n");
    }

    // ────────────────────────────────────────────────────────
    bool Initialize(HINSTANCE hInst,
        LRESULT(CALLBACK* proc)(HWND, UINT, WPARAM, LPARAM))
    {
        if (!Win.Initialize(hInst, proc))             return false;
        if (!Gfx.Init(Win.hWnd, SCREEN_W, SCREEN_H)) return false;

        Cam.Init(&Gfx, SCREEN_W, SCREEN_H);
        CoInitialize(nullptr);
        TexCache.Init(&Gfx);
        srand((unsigned)GetTickCount64());

        // ── 셰이더 ──
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

        // ── 메쉬 ──
        QuadMesh = new Mesh(); QuadMesh->CreateQuad(&Gfx, -0.5f, -0.5f, 0.5f, 0.5f);
        BarMesh = new Mesh(); BarMesh->CreateQuad(&Gfx, 0.0f, 0.0f, 1.0f, 1.0f);

        // ── 플레이어 머티리얼 ──
        MatPlayer = new ColorMaterial(DefaultShaders, { 0.3f, 0.6f, 1.0f, 1.0f }, &Gfx);
        MatPlayerFly = new ColorMaterial(DefaultShaders, { 0.3f, 0.8f, 1.0f, 1.0f }, &Gfx);
        MatPlayerPT = new ColorMaterial(DefaultShaders, { 0.4f, 1.0f, 0.5f, 1.0f }, &Gfx);
        MatPlayerShld = new ColorMaterial(DefaultShaders, { 1.0f, 0.3f, 0.3f, 1.0f }, &Gfx);
        MatChargeBar = new ColorMaterial(DefaultShaders, { 1.0f, 0.9f, 0.0f, 1.0f }, &Gfx);

        auto* playerTex = TexCache.Get(L"player.png");
        if (playerTex) MatPlayer->SetTexture(playerTex);

        auto* srvFly = TexCache.Get(L"player_fly.png");
        auto* srvPT = TexCache.Get(L"player_passthrough.png");
        auto* srvShld = TexCache.Get(L"player_shield.png");
        if (srvFly)  MatPlayerFly->SetTexture(srvFly);
        if (srvPT)   MatPlayerPT->SetTexture(srvPT);
        if (srvShld) MatPlayerShld->SetTexture(srvShld);

        // ── 아이템 머티리얼 ──
        XMFLOAT4 itemColors[ITEM_MAT_COUNT] = {
            { 0.3f, 0.8f, 1.0f, 1.0f },  // [0] Fly       - 하늘색
            { 0.4f, 1.0f, 0.5f, 1.0f },  // [1] PassThrough - 초록
            { 1.0f, 0.3f, 0.3f, 1.0f },  // [2] Shield    - 빨강
            { 1.0f, 0.9f, 0.2f, 1.0f },  // [3] Checkpoint - 노랑
        };
        const wchar_t* itemTex[ITEM_MAT_COUNT] = {
            L"item_fly.png", L"item_passthrough.png",
            L"item_shield.png", L"item_checkpoint.png"
        };
        for (int i = 0; i < ITEM_MAT_COUNT; ++i)
        {
            MatItemBar[i] = new ColorMaterial(DefaultShaders, itemColors[i], &Gfx);
            MatRouletteIcon[i] = new ColorMaterial(DefaultShaders, itemColors[i], &Gfx);
            auto* srv = TexCache.Get(itemTex[i]);
            if (srv)
            {
                MatItemBar[i]->SetTexture(srv);
                MatRouletteIcon[i]->SetTexture(srv);
            }
        }

        // ── 깃발 머티리얼 ──
        MatFlagMast = new ColorMaterial(DefaultShaders, { 0.7f, 0.5f, 0.2f, 1.0f }, &Gfx);
        MatFlagFlag = new ColorMaterial(DefaultShaders, { 1.0f, 0.2f, 0.2f, 1.0f }, &Gfx);
        MatFlagMast->SetTexture(TexCache.Get(L"flag_mast.png"));
        MatFlagFlag->SetTexture(TexCache.Get(L"flag_flag.png"));

        BuildMap();
        BuildRandomBoxes();
        BuildFlag();
        BuildPlayer();
        BuildGoal();

        // 플레이어 → 랜덤박스에 주입
        if (!World.empty())
        {
            auto* player = World[0];
            auto* pc = player->GetComponent<PlayerController>();
            for (auto* go : RandomBoxes)
            {
                auto* rb = go->GetComponent<RandomBoxComp>();
                if (rb) { rb->Player = player; rb->PC = pc; }
            }
        }

        printf("[Engine] Ready.\n");
        printf("  Arrow L/R : Move\n");
        printf("  Space     : Jump (hold = charge, release = launch)\n");
        printf("  F : Fly   R : Reset   C : Checkpoint   ESC : Quit\n");
        printf("  LClick : Print world coord\n");
        printf("  [아이템] 하늘=Fly  초록=PassThrough  빨강=Shield  노랑=Checkpoint\n");
        return true;
    }

    // ────────────────────────────────────────────────────────
    //  맵 구성
    //  ★ 플랫폼 추가/제거는 여기서만 하면 됩니다.
    //
    //  단색:    AddPlatform(타입, LX, BY, RX, TY);
    //  텍스처:  AddPlatform(타입, LX, BY, RX, TY, L"파일.png");
    // ────────────────────────────────────────────────────────
    void BuildMap()
    {
        // ㅡ 왼쪽 벽 ㅡ
        AddPlatform(PlatformType::Normal, -10.0f, -5.0f, -5.0f, 0.5f, L"stoneWall.png");
        AddPlatform(PlatformType::Normal, -10.0f, 0.0f, -5.0f, 5.0f, L"stoneWall.png");
        AddPlatform(PlatformType::Normal, -10.0f, 5.0f, -5.0f, 10.0f, L"stoneWall.png");
        AddPlatform(PlatformType::Normal, -10.0f, 10.0f, -5.0f, 15.0f, L"stoneWall.png");
        AddPlatform(PlatformType::Normal, -10.0f, 15.0f, -5.0f, 20.0f, L"stoneWall.png");

        // ㅡ 오른쪽 벽 ㅡ
        AddPlatform(PlatformType::Normal, 5.0f, -5.0f, 10.0f, 0.5f, L"stoneWall.png");
        AddPlatform(PlatformType::Normal, 5.0f, 0.0f, 10.0f, 5.0f, L"stoneWall.png");
        AddPlatform(PlatformType::Normal, 5.0f, 5.0f, 10.0f, 10.0f, L"stoneWall.png");
        AddPlatform(PlatformType::Normal, 5.0f, 10.0f, 10.0f, 15.0f, L"stoneWall.png");
        AddPlatform(PlatformType::Normal, 5.0f, 15.0f, 10.0f, 20.0f, L"stoneWall.png");

        AddPlatform(PlatformType::PassThrough, -5.0f, 0.0f, 5.0f, 20.0f, L"background1__.png");
        AddPlatform(PlatformType::PassThrough, -5.0f, 20.0f, 5.0f, 40.0f, L"background2_.png");
        AddPlatform(PlatformType::PassThrough, -5.0f, 40.0f, 5.0f, 60.0f, L"background3_.png");

        // ── 바닥 ──
        AddPlatform(PlatformType::Normal, -5.0f, -5.0f, 5.0f, 0.0f, L"ground2.png");

        AddPlatform(PlatformType::Normal, 1.0f, 1.5f, 2.0f, 1.8f, L"ground1.png");
        AddPlatform(PlatformType::Normal, 3.0f, 2.5f, 4.0f, 2.8f, L"ground1.png");
        AddPlatform(PlatformType::Normal, 1.0f, 3.5f, 2.0f, 3.8f, L"ground1.png");
        AddPlatform(PlatformType::Normal, 3.0f, 4.5f, 4.0f, 4.8f, L"ground1.png");
        AddPlatform(PlatformType::Normal, 1.0f, 5.5f, 2.0f, 5.8f, L"ground1.png");
        AddPlatform(PlatformType::Normal, -0.8f, 5.5f, -0.2f, 5.8f, L"ground1.png");
        AddPlatform(PlatformType::Normal, -2.8f, 5.5f, -2.2f, 5.8f, L"ground1.png");
        AddPlatform(PlatformType::Normal, -3.8f, 6.5f, -3.2f, 6.8f, L"ground1.png");
        AddPlatform(PlatformType::Vanishing, -3.8f, 8.2f, -3.2f, 8.5f, L"vanishing1.png");
        AddPlatform(PlatformType::Normal, -3.8f, 9.9f, -3.2f, 10.2f, L"ground1.png");
        AddPlatform(PlatformType::Normal, -0.6f, 8.2f, -0.2f, 8.5f, L"ground1.png");
        AddPlatform(PlatformType::Ice, 1.8f, 8.2f, 2.2f, 8.5f, L"ice1.png");
        AddPlatform(PlatformType::Ice, 3.8f, 9.7f, 4.2f, 10.0f, L"ice1.png");
        AddPlatform(PlatformType::Ice, 1.8f, 11.2f, 2.2f, 11.5f, L"ice1.png");
        AddPlatform(PlatformType::Ice, 3.8f, 12.7f, 4.2f, 13.0f, L"ice1.png");
        AddPlatform(PlatformType::Moving, -1.0f, 13.7f, 1.2f, 14.0f, L"moving1.png");
        AddPlatform(PlatformType::Moving, -3.0f, 15.5f, -2.5f, 15.8f, L"moving1.png");
        AddPlatform(PlatformType::Moving, -1.0f, 17.5f, -0.5f, 17.8f, L"moving1.png");
        AddPlatform(PlatformType::Moving, -3.0f, 19.5f, -2.5f, 19.8f, L"moving1.png");
        /*
        // ── 1층 ──
        AddPlatform(PlatformType::Normal, -3.0f, 1.5f, 0.0f, 1.8f, L"ground1.png");
        AddPlatform(PlatformType::Ice, 1.0f, 1.5f, 3.5f, 1.8f, L"ice1.png");
        AddPlatform(PlatformType::Normal, -1.0f, 3.5f, 2.0f, 3.8f, L"ground1.png");

        // ── 2층 ──
        AddPlatform(PlatformType::Normal, -4.0f, 5.0f, -1.5f, 5.3f, L"ground1.png");
        AddPlatform(PlatformType::Ice, 0.0f, 5.5f, 3.0f, 5.8f, L"ice1.png");
        AddPlatform(PlatformType::PassThrough, -2.0f, 7.0f, 0.5f, 7.2f, L"passThrough1.png");
        AddPlatform(PlatformType::Normal, 1.5f, 7.0f, 4.0f, 7.3f, L"ground1.png");

        // ── 3층 ──
        AddPlatform(PlatformType::Normal, -3.5f, 9.0f, -2.0f, 9.2f, L"ground1.png");
        AddPlatform(PlatformType::Ice, -0.5f, 9.5f, 1.5f, 9.7f, L"ice1.png");
        AddPlatform(PlatformType::Normal, 2.5f, 10.0f, 4.5f, 10.2f, L"ground1.png");
        AddPlatform(PlatformType::Vanishing, -1.0f, 11.5f, 1.5f, 11.7f, L"vanishing1.png");

        // ── 4층 ──
        AddPlatform(PlatformType::Reverse, -4.0f, 13.0f, -1.0f, 13.3f, L"reverse1.png");
        AddPlatform(PlatformType::Ice, 0.5f, 13.5f, 3.5f, 13.8f, L"ice1.png");
        AddPlatform(PlatformType::Moving, -2.0f, 15.5f, 2.0f, 15.8f, L"moving1.png");

        // ── 5층 ──
        AddPlatform(PlatformType::Normal, -4.5f, 17.0f, -2.0f, 17.3f, L"ground1.png");
        AddPlatform(PlatformType::Ice, 2.0f, 17.3f, 4.5f, 17.6f, L"ice1.png");

        // ── 6층 ──
        AddPlatform(PlatformType::Normal, -3.0f, 19.0f, -1.0f, 19.3f, L"ground1.png");
        AddPlatform(PlatformType::Normal, 1.0f, 19.5f, 3.0f, 19.8f, L"ground1.png");

        // ── 7층 ──
        AddPatternPlatform(PlatformType::Normal, -4.5f, 20.5f, -2.5f, 20.8f, L"ground1.png");
        AddPatternPlatform(PlatformType::Ice, -1.0f, 21.4f, 1.0f, 21.7f, L"ice1.png");
        AddPatternPlatform(PlatformType::Reverse, 2.0f, 22.4f, 4.0f, 22.7f, L"reverse1.png");
        AddPatternPlatform(PlatformType::Normal, 0.0f, 23.4f, 2.0f, 23.7f, L"ground1.png");
        AddPatternPlatform(PlatformType::Ice, -3.0f, 24.4f, -1.0f, 24.7f, L"ice1.png");

        // ── 8층 ──
        AddPlatform(PlatformType::Bomb, -4.5f, 26.0f, -2.5f, 26.3f, L"ground1.png");
        AddPlatform(PlatformType::Moving, -0.5f, 26.5f, 1.5f, 26.8f, L"moving1.png");

        // ── 9층 ──
        AddPlatform(PlatformType::Ice, -3.0f, 29.1f, -0.5f, 29.4f, L"ice1.png");
        AddPlatform(PlatformType::Reverse, 1.5f, 28.5f, 4.0f, 28.8f, L"reverse1.png");

        if (PatternPlatforms.size() >= 2)
        {
          PatternPlatforms[0]->IsActive = true;
          PatternPlatforms[0]->Owner->Visible = true;

          PatternPlatforms[1]->IsActive = true;
          PatternPlatforms[1]->Owner->Visible = true;
        }

        // ── 꼭대기 ──
        AddPlatform(PlatformType::Normal, -1.5f, 17.5f, 1.5f, 17.8f, L"ground1.png");
        

        AddPlatform(PlatformType::Normal, -1.5f, 31.3f, 1.5f, 31.6f, L"ground1.png");
        */
    }

    // ────────────────────────────────────────────────────────
    //  플랫폼 생성 헬퍼
    //  ★ 새 PlatformType 추가 시 switch 에 case 만 추가하면 됩니다.
    // ────────────────────────────────────────────────────────
    void AddPlatform(PlatformType type,
        float lx, float by, float rx, float ty,
        const wchar_t* texPath = nullptr)
    {
        float w = rx - lx;
        float h = ty - by;
        float cx = (lx + rx) * 0.5f;
        float cy = (by + ty) * 0.5f;

        XMFLOAT4 color = { 1, 1, 1, 1 };
        switch (type)
        {
        case PlatformType::Normal:      color = { 0.5f, 0.4f, 0.3f, 1.0f }; break;
        case PlatformType::Ice:         color = { 0.6f, 0.9f, 1.0f, 1.0f }; break;
        case PlatformType::PassThrough: color = { 0.4f, 0.8f, 0.4f, 1.0f }; break;
        case PlatformType::Vanishing:   color = { 1.0f, 0.3f, 0.3f, 1.0f }; break;
        case PlatformType::Reverse:     color = { 0.8f, 0.3f, 1.0f, 1.0f }; break;
        case PlatformType::Moving:      color = { 1.0f, 0.8f, 0.2f, 1.0f }; break;
        case PlatformType::Bomb:        color = { 1.0f, 0.5f, 0.2f, 1.0f }; break;
        }

        auto* mat = new ColorMaterial(DefaultShaders, color, &Gfx);
        OwnedMaterials.push_back(mat);

        auto* srv = TexCache.Get(texPath);
        if (srv) mat->SetTexture(srv);

        auto* go = new GameObject(cx, cy);
        go->Scale = { w, h };
        go->AddComponent(new MeshRenderer(QuadMesh, mat));
        go->AddComponent(new PlatformComp(type));

        Platforms.push_back(go);
    }

    void AddPatternPlatform(
      PlatformType type,
      float lx, float by, float rx, float ty,
      const wchar_t* texPath = nullptr)
    {
      float w = rx - lx;
      float h = ty - by;
      float cx = (lx + rx) * 0.5f;
      float cy = (by + ty) * 0.5f;

      XMFLOAT4 color = { 1,1,1,1 };

      switch (type)
      {
      case PlatformType::Normal:      color = { 0.5f, 0.4f, 0.3f, 1.0f }; break;
      case PlatformType::Ice:         color = { 0.6f, 0.9f, 1.0f, 1.0f }; break;
      case PlatformType::PassThrough: color = { 0.4f, 0.8f, 0.4f, 1.0f }; break;
      case PlatformType::Vanishing:   color = { 1.0f, 0.3f, 0.3f, 1.0f }; break;
      case PlatformType::Reverse:     color = { 0.8f, 0.3f, 1.0f, 1.0f }; break;
      case PlatformType::Moving:      color = { 1.0f, 0.8f, 0.2f, 1.0f }; break;
      case PlatformType::Bomb:        color = { 1.0f, 0.5f, 0.2f, 1.0f }; break;
      }

      auto* mat = new ColorMaterial(DefaultShaders, color, &Gfx);
      OwnedMaterials.push_back(mat);

      auto* srv = TexCache.Get(texPath);
      if (srv) mat->SetTexture(srv);

      auto* go = new GameObject(cx, cy);
      go->Scale = { w, h };

      go->AddComponent(new MeshRenderer(QuadMesh, mat));

      auto* plat = new PlatformComp(type);

      // 처음엔 비활성
      plat->IsActive = false;
      go->Visible = false;

      go->AddComponent(plat);

      Platforms.push_back(go);

      // 패턴 플랫폼 등록
      PatternPlatforms.push_back(plat);
    }

    // ── 랜덤박스 배치 ────────────────────────────────────────
    void BuildRandomBoxes()
    {
        AddRandomBox(3.0f, 6.0f);
        AddRandomBox(1.0f, 14.0f);
        /*
        AddRandomBox(0.0f, 2.5f);
        AddRandomBox(3.0f, 6.0f);
        AddRandomBox(-2.0f, 10.0f);
        AddRandomBox(1.0f, 14.0f);
        */
    }

    void AddRandomBox(float cx, float cy,
        const wchar_t* texPath = L"item_randombox.png")
    {
        auto* mat = new ColorMaterial(DefaultShaders, { 1.0f, 0.8f, 0.2f, 1.0f }, &Gfx);
        OwnedMaterials.push_back(mat);
        auto* srv = TexCache.Get(texPath);
        if (srv) mat->SetTexture(srv);

        auto* go = new GameObject(cx, cy);
        go->Scale = { 0.4f, 0.4f };
        go->AddComponent(new MeshRenderer(QuadMesh, mat));
        go->AddComponent(new RandomBoxComp());

        RandomBoxes.push_back(go);
    }

    // ── 체크포인트 깃발 오브젝트 생성 ───────────────────────
    void BuildFlag()
    {
        FlagObject = new GameObject(0.0f, 0.0f);
        FlagObject->Active = false;

        auto* cfr = new CheckpointFlagRenderer();
        cfr->MastMesh = QuadMesh;
        cfr->FlagMesh = QuadMesh;
        cfr->MatMast = MatFlagMast;
        cfr->MatFlag = MatFlagFlag;
        FlagObject->AddComponent(cfr);
    }

    // ── 플레이어 조립 ────────────────────────────────────────
    void BuildPlayer()
    {
        // [플레이어 시작 위치]
        //  ─ 카메라 시작 위치와 동일하게 잡아서 화면 중앙에 배치
        const float startX = 0.0f;
        const float startY = 1.5f;

        auto* player = new GameObject(startX, startY);
        player->Scale = { 0.6f, 0.6f };

        player->AddComponent(new MeshRenderer(QuadMesh, MatPlayer));

        auto* pc = new PlayerController();
        pc->Platforms = &Platforms;
        pc->ItemState = &ItemStateData;
        pc->Checkpoint = { startX, startY };
        pc->CheckpointFlag = FlagObject;
        player->AddComponent(pc);

        pc->MatNormal = MatPlayer;
        pc->MatFly = MatPlayerFly;
        pc->MatPassThrough = MatPlayerPT;
        pc->MatShield = MatPlayerShld;

        player->AddComponent(new JumpChargeBar(BarMesh, MatChargeBar));

        auto* aib = new ActiveItemBar(QuadMesh);
        for (int i = 0; i < ActiveItemBar::BAR_COUNT && i < ITEM_MAT_COUNT; ++i)
            aib->MatBars[i] = MatItemBar[i];
        player->AddComponent(aib);

        auto* rui = new RouletteUI();
        rui->pQuad = QuadMesh;
        for (int i = 0; i < RouletteState::ITEM_COUNT && i < ITEM_MAT_COUNT; ++i)
            rui->MatIcon[i] = MatRouletteIcon[i];
        player->AddComponent(rui);

        World.push_back(player);

        // [카메라 점프킹 모드 초기화]
        //  ─ X는 시작점에 고정, Y는 시작점이 최솟값
        Cam.FixedX = startX;
        Cam.StartY = startY;
        Cam.Pos = { startX, startY };
    }

    // ── 골(클리어 지점) 조립 ─────────────────────────────────
    void BuildGoal()
    {
        if (World.empty()) return;
        GameObject* player = World[0];

        // 골 마커 머티리얼 (goal.png 있으면 텍스처, 없으면 금색 사각형) //지우면 아예 보이지 않고 판정은 살아있음
        auto* matGoal = new ColorMaterial(DefaultShaders, { 1.0f, 0.85f, 0.2f, 1.0f }, &Gfx);
        OwnedMaterials.push_back(matGoal);
        if (auto* srv = TexCache.Get(L"goal.png")) matGoal->SetTexture(srv);

        // 미션 배너 머티리얼 (MISSION COMPLETE 이미지)
        auto* matMission = new ColorMaterial(DefaultShaders, { 1, 1, 1, 1 }, &Gfx);
        OwnedMaterials.push_back(matMission);
        if (auto* srv = TexCache.Get(L"mission_complete.png")) matMission->SetTexture(srv);

        // [클리어 지점] ★ 이 좌표만 바꾸면 골 위치가 바뀝니다.
        //   좌클릭으로 원하는 위치의 월드 좌표를 콘솔에 찍어보고 그 값을 넣으세요.
        auto* goal = new GameObject(-2.75f, 20.4f);   // 현재 맵 꼭대기 근처
        goal->Scale = { 2.0f, 0.4f };                 // 닿을 영역 크기

        goal->AddComponent(new MeshRenderer(QuadMesh, matGoal));   // 보이는 마커 //지우면 아예 보이지 않고 판정은 살아있음

        auto* gc = new GoalComp();
        gc->Player = player;
        gc->Cleared = &Cleared;
        goal->AddComponent(gc);                                    // 판정

        auto* banner = new MissionBanner();
        banner->Cam = &Cam;
        banner->Cleared = &Cleared;
        banner->Mat = matMission;
        banner->Quad = QuadMesh;
        goal->AddComponent(banner);                                // 표시

        World.push_back(goal);   // World 에 넣으면 Update/Render/삭제 자동
    }

    // ────────────────────────────────────────────────────────
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

        for (auto* go : World)       go->Input();
        for (auto* go : Platforms)   go->Input();
        for (auto* go : RandomBoxes) go->Input();
    }

    void UpdatePatternPlatforms(float dt)
    {
      if (PatternPlatforms.size() < 2)
        return;

      PatternTimer += dt;

      int current = PatternIndex;
      int next = (PatternIndex + 1) % PatternPlatforms.size();

      // ---------------------------
      // 사라질 플랫폼만 깜빡임
      // ---------------------------

      float blinkStart = PatternInterval - 0.2f;

      if (PatternTimer >= blinkStart)
      {
        bool visible =
          ((int)(PatternTimer * 10.0f) % 2) == 0;

        PatternPlatforms[current]->Owner->Visible = visible;

        // 다음 플랫폼은 계속 보임
        PatternPlatforms[next]->Owner->Visible = true;
      }
      else
      {
        PatternPlatforms[current]->Owner->Visible = true;
        PatternPlatforms[next]->Owner->Visible = true;
      }

      // ---------------------------
      // 패턴 전환
      // ---------------------------

      if (PatternTimer >= PatternInterval)
      {
        PatternTimer = 0.0f;

        PatternIndex++;

        if (PatternIndex >= PatternPlatforms.size())
          PatternIndex = 0;

        // 전부 끄기
        for (auto* plat : PatternPlatforms)
        {
          plat->IsActive = false;
          plat->Owner->Visible = false;
        }

        current = PatternIndex;
        next = (PatternIndex + 1) % PatternPlatforms.size();

        // 현재 + 다음 활성화
        PatternPlatforms[current]->IsActive = true;
        PatternPlatforms[current]->Owner->Visible = true;

        PatternPlatforms[next]->IsActive = true;
        PatternPlatforms[next]->Owner->Visible = true;
      }
    }

    void Update()
    {
        float dt = Timer.Get();

        UpdatePatternPlatforms(dt);

        for (auto* go : Platforms)   go->Update(dt, &Gfx);
        for (auto* go : World)       go->Update(dt, &Gfx);
        for (auto* go : RandomBoxes) go->Update(dt, &Gfx);
        if (FlagObject) FlagObject->Update(dt, &Gfx);
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
        for (auto* go : Platforms)   go->Render(&Gfx);
        for (auto* go : RandomBoxes) go->Render(&Gfx);
        if (FlagObject) FlagObject->Render(&Gfx);
        for (auto* go : World)       go->Render(&Gfx);

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