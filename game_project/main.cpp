/// ============================================================
//  JumpKing-Style Platformer  |  DirectX 11  |  Single File
//
//  ★ 확장 가이드 ★
//  ─────────────────────────────────────────────────────────
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
//  전방 선언
// ============================================================
class GraphicsContext;
class GameObject;

// ============================================================
//  화면 상수
// ============================================================
static constexpr int SCREEN_W = 800;
static constexpr int SCREEN_H = 600;

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
static constexpr float GROUND_ACCEL = 10.0f;  // 지면 가속도 (높을수록 빠르게 최고속 도달)
static constexpr float GROUND_DECEL = 20.0f;  // 지면 감속도 (입력 없을 때)

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
  Moving
};

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

        // 포인트 샘플러 (픽셀아트 테두리 번짐 방지)
        D3D11_SAMPLER_DESC smpDesc = {};
        smpDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
        smpDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
        smpDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
        smpDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
        Device->CreateSamplerState(&smpDesc, &Sampler);

        // 알파 블렌드 (PNG 투명 처리)
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
//  ─ 같은 파일 경로는 한 번만 로드합니다.
//  ─ 소멸자에서 모든 SRV 를 일괄 해제합니다.
// ============================================================
class TextureCache
{
    GraphicsContext* gfx = nullptr;
    std::unordered_map<std::wstring, ID3D11ShaderResourceView*> cache;
public:
    void Init(GraphicsContext* g) { gfx = g; }

    // path == nullptr 이면 nullptr 반환 (텍스처 없음 → 단색 폴백)
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

    // 텍스처를 연결하면 tintColor 를 흰색(중립)으로 자동 변경
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
    bool Visible = true; //Vanishing용

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
    Vec2  Pos = { 0, 0 };
    float ViewW = 0;
    float ViewH = 0;
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
//  ─ AABB 를 GameObject 의 Pos/Scale 에서 계산합니다.
//    이렇게 하면 나중에 플랫폼을 움직여도 충돌이 따라옵니다.
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

    PlatformComp(PlatformType t) : Type(t) {}

    // Moving platform용
    void Start(GraphicsContext* gfx) override
    {
      // 시작 위치 저장
      StartPos = Owner->Pos;
    }

    void Update(float dt) override
    {
      // ─────────────────────────────
      // Vanishing 플랫폼
      // ─────────────────────────────
      if (Type == PlatformType::Vanishing)
      {
        // 한번 밟힌 이후 타이머 진행
        if (Triggered)
        {
          Timer += dt;

          // 3초 후 사라짐
          if (Timer >= 3.0f && IsActive)
          {
            IsActive = false;

            // 화면에서도 숨김
            Owner->Visible = false;
          }

          // 5초 후 다시 생성
          if (Timer >= 5.0f)
          {
            IsActive = true;
            Owner->Visible = true;

            Triggered = false;
            Timer = 0.0f;
          }
        }
      }

      // ─────────────────────────────
      // Moving 플랫폼
      // ─────────────────────────────
      if (Type == PlatformType::Moving)
      {
        // 시간 증가
        MoveTimer += dt * MoveSpeed;

        // sin 기반 좌우 왕복 이동
        Owner->Pos.x =
          StartPos.x + sinf(MoveTimer) * MoveRange;
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
//  PlayerController
//  ─ Platforms 포인터를 외부에서 주입받습니다 (전역 제거).
//  ─ 이동 처리를 UpdateMovement() 로 분리했습니다.
//    점프 충전 중 이동 차단은 타입과 무관하게 1단계에서 처리되므로
//    새 타입을 추가해도 이 동작은 자동으로 적용됩니다.
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

    // ── 물리 상태 ──
    Vec2  Vel = { 0, 0 };
    bool  OnGround = false;
    float HalfW = 0.3f;
    float HalfH = 0.3f;

    // ── 점프 상태 (충전 시스템) ──
    bool  PrevSpace = false;
    bool  SpacePressedThisFrame = false;
    bool  SpaceHeld = false;
    float JumpCharge = 0.0f;
    bool  JumpedThisPress = false;

    // ── 고급 조작감 타이머 (플랫포머 필수 기술) ──
    float CoyoteTimer = 0.0f;        // 코요테 타임 
    float JumpBufferTimer = 0.0f;    // 점프 버퍼링 
    float CurrentShakeX = 0.0f;      // 흔들림 연출 누적 방지용 변수

    // ── 점프킹 특화 상태 (조작 불가) ──
    bool  IsStunned = false;

    // ── 입력 ──
    float MoveInput = 0.0f;

    // ── 지면 타입 플래그 ──
    bool  OnIce = false;
    float ReverseTimer = 0.0f;
    bool IsReverse() const { return ReverseTimer > 0.0f; }

    // ── 튕김(Bounce) 설정 ──
    float BounceFactor = 0.85f;   // 튕길 때 보존되는 속도 비율 
    float MinBounceSpeed = 3.0f;  // 살짝 부딪혀도 무조건 튕겨나가는 최소 속도

    // ── 개발 도구 ──
    bool  FlyMode = false;
    Vec2  Checkpoint = { 0, 0.5f };

    // ── 엣지 감지 ──
    bool PrevF = false, PrevR = false, PrevC = false;

    // ────────────────────────────────────────────────────────
    void Input() override
    {
        bool spaceNow = (GetAsyncKeyState(VK_SPACE) & 0x8000) != 0;
        SpacePressedThisFrame = spaceNow && !PrevSpace;
        SpaceHeld = spaceNow;
        PrevSpace = spaceNow;

        bool fNow = (GetAsyncKeyState('F') & 0x8000) != 0;
        bool rNow = (GetAsyncKeyState('R') & 0x8000) != 0;
        bool cNow = (GetAsyncKeyState('C') & 0x8000) != 0;

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

    // ────────────────────────────────────────────────────────
    void Update(float dt) override
    {
        if (FlyMode) { UpdateFly(dt); return; }

        Vel.y += GRAVITY * dt;

        // Reverse 효과 시간 감소
        if (ReverseTimer > 0.0f)
        {
          ReverseTimer -= dt;
        }

        UpdateMovement(dt);
        UpdateJump(dt);

        // 순수 물리 이동만 적용
        Owner->Pos.x += Vel.x * dt;
        Owner->Pos.y += Vel.y * dt;

        // 충돌 해결 (흔들림 없이 정직하게 계산하므로 버그 없음)
        ResolveAllCollisions(dt);

        // ── ★ 오직 Render 컴포넌트가 참고할 흔들림 수치만 계산 ──
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

        // 공중에서 캐릭터가 회전하는 물리 연출
        if (!OnGround)
        {
            Owner->Rot -= Vel.x * 3.0f * dt;
        }
        else
        {
            Owner->Rot += (0.0f - Owner->Rot) * 15.0f * dt;
        }
    }

private:
    // ── 수평 이동 (캐릭터가 점프 시 공중에서도 좌우 이동 가능하게 구현) ─────────────────────────
    void UpdateMovement(float dt)
    {
        // 지면에서 점프 충전 중일 때는 좌우 이동을 완벽 차단하고 브레이크
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
              // 입력 반전
              float reverseInput = IsReverse() ? -MoveInput : MoveInput;

              if (reverseInput != 0.0f && !IsStunned)
              {
                Vel.x += reverseInput * GROUND_ACCEL * dt;

                Vel.x = max(-MOVE_SPEED, min(MOVE_SPEED, Vel.x));
              }
              else
              {
                float decel = GROUND_DECEL * dt;

                if (Vel.x > 0)
                  Vel.x = max(0.0f, Vel.x - decel);
                else
                  Vel.x = min(0.0f, Vel.x + decel);
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
          // 공중 상태 조작 가능
          if (IsStunned)
            return;

          float airAccel = 12.0f;
          float airDecel = 2.0f;

          // Reverse 상태면 공중 이동도 반전
          float input =
            IsReverse() ? -MoveInput : MoveInput;

          if (input != 0.0f)
          {
            Vel.x += input * airAccel * dt;

            Vel.x = max(-MOVE_SPEED, min(MOVE_SPEED, Vel.x));
          }
          else
          {
            if (Vel.x > 0)
              Vel.x = max(0.0f, Vel.x - airDecel * dt);
            else
              Vel.x = min(0.0f, Vel.x + airDecel * dt);
          }
        }
    }

    // ── 점프 (코요테 타임 & 버퍼링 적용) ────────────────────────
    void UpdateJump(float dt)
    {
        // 1. 코요테 타임 업데이트 (발이 허공에 떠도 0.1초간 점프 가능)
        if (OnGround) CoyoteTimer = 0.1f;
        else if (CoyoteTimer > 0.0f) CoyoteTimer -= dt;

        // 2. 점프 버퍼링 업데이트 (착지 직전 0.15초 내의 스페이스바 입력을 저장)
        if (SpacePressedThisFrame) JumpBufferTimer = 0.15f;
        else if (JumpBufferTimer > 0.0f) JumpBufferTimer -= dt;

        // 코요테 타임 유예 기간이 남아있다면 공중이라도 점프 가능 상태로 취급
        bool canJump = (OnGround || CoyoteTimer > 0.0f) && !IsStunned;

        if (canJump)
        {
            // 충전 시작 (눌러서 충전하거나, 공중 버퍼링 타이머가 살아있을 때)
            if ((SpaceHeld || JumpBufferTimer > 0.0f) && !JumpedThisPress)
            {
                JumpCharge = min(JumpCharge + JUMP_CHARGE * dt, 1.0f);
            }

            // 발사 (스페이스바를 떼는 순간 충전된 양만큼 튀어오름)
            if (!SpaceHeld && JumpCharge > 0.0f)
            {
                float speed = JUMP_MIN + (JUMP_MAX - JUMP_MIN) * JumpCharge;
                Vel.y = speed;

                // [공중 제어 대응] 점프 순간 기존 달리기 관성을 지우고 입력 방향으로 발사
                Vel.x = MoveInput * MOVE_SPEED;

                // Reverse 플랫폼이면 점프 방향도 반전
                float jumpInput = IsReverse() ? -MoveInput : MoveInput;

                Vel.x = jumpInput * MOVE_SPEED;

                JumpCharge = 0.0f;
                JumpedThisPress = true;
                OnGround = false;

                CoyoteTimer = 0.0f;      // 점프를 뜄으므로 유예 시간 소모
                JumpBufferTimer = 0.0f;  // 예약된 점프 처리 완료
            }
        }
        else
        {
            if (!SpaceHeld) JumpedThisPress = false;
        }
    }

    // ── 충돌 해결 (점프킹 스타일 핀볼 연쇄 반사) ─────────────────────────
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

        // 비활성 플랫폼은 충돌 무시
        if (!plat->IsActive) return;

        if (!player.Overlaps(platform)) return;

        // 투과 플랫폼(PassThrough) 특수 예외 처리
        if (plat->Type == PlatformType::PassThrough)
        {
            float penetrationY = platform.top - player.bottom;

            // X축 가장자리 낌 (무한 로딩 렉) 방지를 위한 가로 마진 검사
            float marginX = HalfW * 0.5f;
            bool isWithinX = (player.right - marginX > platform.left) &&
                (player.left + marginX < platform.right);

            // 안전 범위 안이고 떨어지는 중일 때만 발판 안착
            if (isWithinX && Vel.y <= 0.0f && penetrationY > 0.0f && penetrationY <= max(0.2f, -Vel.y * dt * 2.0f))
            {
                Owner->Pos.y = platform.top + HalfH;
                Vel.y = 0;
                OnGround = true;

                // PassThrough 착지 시 Reverse 해제
                ReverseTimer = 0.0f;
            }
            return;
        }

        // 일반 및 얼음 발판 충돌 해결 (연쇄 튕김 극대화 버전)
        float oL = player.right - platform.left;
        float oR = platform.right - player.left;
        float oB = player.top - platform.bottom;
        float oT = platform.top - player.bottom;

        float minO = min(min(oL, oR), min(oB, oT));

        // 점프킹의 하드코어한 연쇄 튕김을 구현하기 위한 탄성 수치
        const float HORIZONTAL_BOUNCE = 1.3f; // 벽에 박으면 박은 속도보다 1.3배 더 강하게 반사(도핑)하여 핀볼 효과 극대화

        // 공중에서 속도가 붙은 채로 벽 옆구리에 스치면 무조건 벽 반사 판정을 수직 판정보다 우선함
        bool hitSideWall = (minO == oL || minO == oR) || (abs(Vel.x) > 1.0f && minO != oT);

        if (hitSideWall && !OnGround)
        {
            if (oL < oR) // 왼쪽 벽 옆구리 박음 (오른쪽 이동 중)
            {
                Owner->Pos.x = platform.left - HalfW;
                if (Vel.x >= 0.0f)
                {
                    float speed = max(MinBounceSpeed, Vel.x);
                    Vel.x = -speed * HORIZONTAL_BOUNCE; // 반대편 허공으로 팍 튕겨나감
                    if (Vel.y > 0.0f) Vel.y *= 0.6f;    // 상승력이 꺾여 대각선 아래로 통통 떨어지도록 유도
                }
            }
            else // 오른쪽 벽 옆구리 박음 (왼쪽 이동 중)
            {
                Owner->Pos.x = platform.right + HalfW;
                if (Vel.x <= 0.0f)
                {
                    float speed = max(MinBounceSpeed, -Vel.x);
                    Vel.x = speed * HORIZONTAL_BOUNCE;
                    if (Vel.y > 0.0f) Vel.y *= 0.6f;
                }
            }
        }
        else
        {
            // 상하 수직 충돌 해결
            if (minO == oT)
            {
                // [지면 착지]
                if (Vel.y <= 0.0f)
                {
                    Owner->Pos.y = platform.top + HalfH;
                    Vel.y = 0;
                    OnGround = true;
                    if (plat->Type == PlatformType::Ice) OnIce = true;
                    
                    // Reverse 플랫폼 밟으면 2초간 반전
                    if (plat->Type == PlatformType::Reverse)
                    {
                      ReverseTimer = 2.0f;
                    }
                    else
                    {
                      // 다른 플랫폼 밟으면 Reverse 해제
                      ReverseTimer = 0.0f;
                    }

                    // Vanishing 플랫폼이면 타이머 시작
                    if (plat->Type == PlatformType::Vanishing)
                    {
                      plat->Triggered = true;
                    }
                }
            }
            else if (minO == oB)
            {
                // [천장 박음] 쿵 하고 아래로 정직하게 바운스 처리 (선언해두신 BounceFactor 멤버 활용)
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
        Owner->Pos.x += dir.x * FLY_SPEED * dt;
        Owner->Pos.y += dir.y * FLY_SPEED * dt;
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

    // ── 공유 GPU 리소스 ──
    ShaderSet      DefaultShaders;
    Mesh* QuadMesh = nullptr;   // 단위 쿼드 [-0.5, 0.5]
    Mesh* BarMesh = nullptr;   // 충전 바 쿼드 [0, 1]

    // ── 플레이어 전용 머티리얼 (GameLoop 소유) ──
    ColorMaterial* MatPlayer = nullptr;
    ColorMaterial* MatChargeBar = nullptr;

    // ── 플랫폼 머티리얼 목록 (AddPlatform 에서 생성, 여기서 소유) ──
    std::vector<Material*>   OwnedMaterials;

    bool MousePressed = false;

    GameLoop() { printf("[Engine] GameLoop Created.\n"); }
    ~GameLoop()
    {
        for (auto* go : World)     delete go;
        for (auto* go : Platforms) delete go;
        for (auto* m : OwnedMaterials) delete m;
        // TexCache 소멸자가 SRV 자동 해제
        CoUninitialize();
        delete MatPlayer;
        delete MatChargeBar;
        delete QuadMesh;
        delete BarMesh;
        DefaultShaders.Release();
        printf("[Engine] All resources released.\n");
    }

    // ────────────────────────────────────────────────────────
    bool Initialize(HINSTANCE hInst,
        LRESULT(CALLBACK* proc)(HWND, UINT, WPARAM, LPARAM))
    {
        if (!Win.Initialize(hInst, proc))      return false;
        if (!Gfx.Init(Win.hWnd, SCREEN_W, SCREEN_H)) return false;

        Cam.Init(&Gfx, SCREEN_W, SCREEN_H);
        CoInitialize(nullptr);
        TexCache.Init(&Gfx);

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
        QuadMesh = new Mesh();
        QuadMesh->CreateQuad(&Gfx, -0.5f, -0.5f, 0.5f, 0.5f);

        BarMesh = new Mesh();
        BarMesh->CreateQuad(&Gfx, 0.0f, 0.0f, 1.0f, 1.0f);

        // ── 플레이어 머티리얼 ──
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

    // ────────────────────────────────────────────────────────
    //  맵 구성
    //  ★ 플랫폼 추가/제거는 여기서만 하면 됩니다.
    //
    //  단색:    AddPlatform(타입, LX, BY, RX, TY);
    //  텍스처:  AddPlatform(타입, LX, BY, RX, TY, L"파일.png");
    // ────────────────────────────────────────────────────────
    void BuildMap()
    {
        // ═══════════════════════════════════════════════════════════
        //  왼쪽 벽 (10개 세그먼트, y = -5 ~ 45)
        // ═══════════════════════════════════════════════════════════
        AddPlatform(PlatformType::Normal, -10.0f, -5.0f, -5.0f, 0.0f, L"stoneWall.png");
        AddPlatform(PlatformType::Normal, -10.0f, 0.0f, -5.0f, 5.0f, L"stoneWall.png");
        AddPlatform(PlatformType::Normal, -10.0f, 5.0f, -5.0f, 10.0f, L"stoneWall.png");
        AddPlatform(PlatformType::Normal, -10.0f, 10.0f, -5.0f, 15.0f, L"stoneWall.png");
        AddPlatform(PlatformType::Normal, -10.0f, 15.0f, -5.0f, 20.0f, L"stoneWall.png");
        AddPlatform(PlatformType::Normal, -10.0f, 20.0f, -5.0f, 25.0f, L"stoneWall.png");
        AddPlatform(PlatformType::Normal, -10.0f, 25.0f, -5.0f, 30.0f, L"stoneWall.png");
        AddPlatform(PlatformType::Normal, -10.0f, 30.0f, -5.0f, 35.0f, L"stoneWall.png");
        AddPlatform(PlatformType::Normal, -10.0f, 35.0f, -5.0f, 40.0f, L"stoneWall.png");
        AddPlatform(PlatformType::Normal, -10.0f, 40.0f, -5.0f, 45.0f, L"stoneWall.png");

        // ═══════════════════════════════════════════════════════════
        //  오른쪽 벽 (10개 세그먼트)
        // ═══════════════════════════════════════════════════════════
        AddPlatform(PlatformType::Normal, 5.0f, -5.0f, 10.0f, 0.0f, L"stoneWall.png");
        AddPlatform(PlatformType::Normal, 5.0f, 0.0f, 10.0f, 5.0f, L"stoneWall.png");
        AddPlatform(PlatformType::Normal, 5.0f, 5.0f, 10.0f, 10.0f, L"stoneWall.png");
        AddPlatform(PlatformType::Normal, 5.0f, 10.0f, 10.0f, 15.0f, L"stoneWall.png");
        AddPlatform(PlatformType::Normal, 5.0f, 15.0f, 10.0f, 20.0f, L"stoneWall.png");
        AddPlatform(PlatformType::Normal, 5.0f, 20.0f, 10.0f, 25.0f, L"stoneWall.png");
        AddPlatform(PlatformType::Normal, 5.0f, 25.0f, 10.0f, 30.0f, L"stoneWall.png");
        AddPlatform(PlatformType::Normal, 5.0f, 30.0f, 10.0f, 35.0f, L"stoneWall.png");
        AddPlatform(PlatformType::Normal, 5.0f, 35.0f, 10.0f, 40.0f, L"stoneWall.png");
        AddPlatform(PlatformType::Normal, 5.0f, 40.0f, 10.0f, 45.0f, L"stoneWall.png");

        // ═══════════════════════════════════════════════════════════
        //  바닥
        // ═══════════════════════════════════════════════════════════
        AddPlatform(PlatformType::Normal, -5.0f, -5.0f, 5.0f, 0.0f, L"ground2.png");

        // ───── 🪨 던전 하층 ─────

        // 1층
        AddPlatform(PlatformType::Normal, -3.0f, 1.5f, 0.0f, 1.8f, L"ground1.png");
        AddPlatform(PlatformType::Ice, 1.0f, 1.5f, 3.5f, 1.8f, L"ice1.png");
        AddPlatform(PlatformType::Normal, -1.0f, 3.5f, 2.0f, 3.8f, L"ground1.png");

        // 2층
        AddPlatform(PlatformType::Normal, -4.0f, 5.0f, -1.5f, 5.3f, L"ground1.png");
        AddPlatform(PlatformType::Ice, 0.0f, 5.5f, 3.0f, 5.8f, L"ice1.png");
        AddPlatform(PlatformType::PassThrough, -2.0f, 7.0f, 0.5f, 7.2f, L"passThrough1.png");
        AddPlatform(PlatformType::Normal, 1.5f, 7.0f, 4.0f, 7.3f, L"ground1.png");

        // 3층
        AddPlatform(PlatformType::Normal, -3.5f, 9.0f, -2.0f, 9.2f);
        AddPlatform(PlatformType::Ice, -0.5f, 9.5f, 1.5f, 9.7f, L"ice1.png");
        AddPlatform(PlatformType::Normal, 2.5f, 10.0f, 4.5f, 10.2f, L"ground1.png");
        AddPlatform(PlatformType::PassThrough, -1.0f, 11.5f, 1.5f, 11.7f);

        // 4층
        AddPlatform(PlatformType::Normal, -4.0f, 13.0f, -1.0f, 13.3f);
        AddPlatform(PlatformType::Ice, 0.5f, 13.5f, 3.5f, 13.8f, L"ice1.png");
        AddPlatform(PlatformType::Normal, -2.0f, 15.5f, 2.0f, 15.8f);

        // ───── ❄️ 던전 중층 ─────

        // 5층 (얼음 등장 비율 상승)
        AddPlatform(PlatformType::Normal, -4.0f, 17.0f, -2.0f, 17.3f, L"ground1.png");
        AddPlatform(PlatformType::Ice, -0.5f, 17.0f, 2.0f, 17.3f, L"ice1.png");
        AddPlatform(PlatformType::PassThrough, -3.0f, 19.5f, 0.5f, 19.7f, L"passThrough1.png");
        AddPlatform(PlatformType::Normal, 2.5f, 19.0f, 4.5f, 19.3f, L"ground1.png");

        // 6층 (얼음 본격 — 좌우 양쪽이 다 얼음)
        AddPlatform(PlatformType::Ice, -3.5f, 21.0f, -1.0f, 21.3f, L"ice1.png");
        AddPlatform(PlatformType::Normal, 1.5f, 21.5f, 3.5f, 21.8f, L"ground1.png");
        AddPlatform(PlatformType::Ice, -2.0f, 23.5f, 0.5f, 23.8f, L"ice1.png");
        AddPlatform(PlatformType::PassThrough, 2.0f, 23.5f, 4.0f, 23.7f, L"passThrough1.png");

        // 7층 (좁아지기 시작, 얼음 정밀 점프)
        AddPlatform(PlatformType::Normal, -3.0f, 25.0f, -1.5f, 25.3f, L"ground1.png");
        AddPlatform(PlatformType::Ice, 1.0f, 25.5f, 3.5f, 25.8f, L"ice1.png");
        AddPlatform(PlatformType::PassThrough, -1.0f, 27.0f, 1.0f, 27.2f, L"passThrough1.png");
        AddPlatform(PlatformType::Normal, 2.5f, 27.5f, 4.0f, 27.8f, L"ground1.png");

        // ───── 🌳 지상 근접 ─────

        // 8층 (잔디 위주, 얼음 1개만 잔존)
        AddPlatform(PlatformType::Normal, -4.0f, 29.0f, -1.5f, 29.3f, L"ground1.png");
        AddPlatform(PlatformType::Ice, 0.5f, 29.5f, 2.5f, 29.8f, L"ice1.png");
        AddPlatform(PlatformType::Normal, -2.0f, 31.5f, 1.0f, 31.8f, L"ground1.png");

        // 9층 (좁고 어려움)
        AddPlatform(PlatformType::Normal, -3.5f, 33.0f, -1.5f, 33.3f, L"ground1.png");
        AddPlatform(PlatformType::PassThrough, 0.0f, 33.5f, 2.5f, 33.7f, L"passThrough1.png");
        AddPlatform(PlatformType::Normal, -1.0f, 35.5f, 1.5f, 35.8f, L"ground1.png");

        // 10층 (최종 챌린지 — 가장 좁음)
        AddPlatform(PlatformType::Normal, -2.5f, 37.0f, 0.0f, 37.3f, L"ground1.png");
        AddPlatform(PlatformType::Ice, 1.0f, 39.1f, 3.5f, 39.4f, L"ice1.png");
        AddPlatform(PlatformType::Normal, -3.5f, 39.5f, -1.0f, 39.8f, L"ground1.png");

        // ═══════════════════════════════════════════════════════════
        //  꼭대기 (목표 지점)
        // ═══════════════════════════════════════════════════════════
        AddPlatform(PlatformType::Normal, -1.5f, 41.5f, 1.5f, 41.8f, L"ground1.png");
    }

    // ────────────────────────────────────────────────────────
    //  플랫폼 생성 헬퍼
    //
    //  texPath == nullptr → 단색 (타입별 기본 색상)
    //  texPath 지정 시    → 해당 텍스처 로드 (중복 로드 방지)
    //
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

        // ── 타입별 기본 색상
        // ★ 새 타입 추가 시 case 추가
        XMFLOAT4 color = { 1, 1, 1, 1 };
        switch (type)
        {
        case PlatformType::Normal:      color = { 0.5f, 0.4f, 0.3f, 1.0f }; break;
        case PlatformType::Ice:         color = { 0.6f, 0.9f, 1.0f, 1.0f }; break;
        case PlatformType::PassThrough: color = { 0.4f, 0.8f, 0.4f, 1.0f }; break;
        case PlatformType::Vanishing:   color = { 1.0f, 0.3f, 0.3f, 1.0f }; break;
        case PlatformType::Reverse:     color = { 0.8f, 0.3f, 1.0f, 1.0f }; break;
        case PlatformType::Moving:      color = { 1.0f, 0.8f, 0.2f, 1.0f }; break;
        }

        // ── 머티리얼 생성 (플랫폼마다 독립)
        auto* mat = new ColorMaterial(DefaultShaders, color, &Gfx);
        OwnedMaterials.push_back(mat); // GameLoop 가 소유권 관리

        // ── 텍스처 (없으면 단색 그대로)
        auto* srv = TexCache.Get(texPath);
        if (srv) mat->SetTexture(srv);

        // ── GameObject 조립
        auto* go = new GameObject(cx, cy);
        go->Scale = { w, h };
        go->AddComponent(new MeshRenderer(QuadMesh, mat));
        go->AddComponent(new PlatformComp(type));

        Platforms.push_back(go);
    }

    // ────────────────────────────────────────────────────────
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
        pc->Checkpoint = { startX, startY };
        player->AddComponent(pc);

        player->AddComponent(new JumpChargeBar(BarMesh, MatChargeBar));

        World.push_back(player);

        // [카메라 점프킹 모드 초기화]
        //  ─ X는 시작점에 고정, Y는 시작점이 최솟값
        Cam.FixedX = startX;
        Cam.StartY = startY;
        Cam.Pos = { startX, startY };
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
