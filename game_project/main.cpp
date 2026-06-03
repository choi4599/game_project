// DirectX 11 기반 점프킹 스타일 플랫포머
// 단일 파일 구성

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

// 전방 선언
class GraphicsContext;
class GameObject;
struct PlayerController;

// 화면 해상도
static constexpr int SCREEN_W = 1280;
static constexpr int SCREEN_H = 720;

// 물리 상수 - 게임 느낌 조정 시 이 값들을 수정
static constexpr float GRAVITY = -18.0f;
static constexpr float JUMP_MAX = 9.0f;
static constexpr float JUMP_MIN = 3.0f;
static constexpr float JUMP_CHARGE = 1.5f;   // 점프 충전 속도
static constexpr float MOVE_SPEED = 2.0f;
static constexpr float ICE_ACCEL = 3.0f;
static constexpr float ICE_DRAG = 0.5f;
static constexpr float AIR_ACCEL = 2.0f;   // 현재 미사용
static constexpr float BRAKE_ACCEL = 10.0f;   // 점프 충전 중 제동 강도
static constexpr float FLY_SPEED = 8.0f;   // 개발자 비행 속도
static constexpr float GROUND_ACCEL = 10.0f;
static constexpr float GROUND_DECEL = 20.0f;

// 아이템 지속 시간 상수
static constexpr float ITEM_FLY_DURATION = 3.0f;
static constexpr float FLY_ITEM_SPEED = 2.0f;   // 아이템 비행 속도 (개발자 비행과 별도)
static constexpr float ITEM_PASSTHROUGH_DURATION = 5.0f;
static constexpr float ITEM_SHIELD_DURATION = 8.0f;
static constexpr float ITEM_PICKUP_RADIUS = 0.5f;   // 랜덤박스 획득 판정 반경

// 룰렛 상수
static constexpr float ROULETTE_DURATION = 2.0f;
static constexpr float ROULETTE_INTERVAL_MIN = 0.05f;  // 초기 슬롯 전환 속도
static constexpr float ROULETTE_INTERVAL_MAX = 0.4f;   // 최종 슬롯 전환 속도 (점점 느려짐)

// 기본 자료형
struct Vec2 { float x = 0, y = 0; };

// 정점 구조체 - 위치, UV, 색상
struct Vertex
{
    XMFLOAT3 pos;
    XMFLOAT2 uv;
    XMFLOAT4 col;
};

// 상수 버퍼 구조체
struct CbWorld { XMMATRIX  matWorld; };
struct CbMaterial { XMFLOAT4  tintColor; int useTexture; XMFLOAT3 pad; };
struct CbCamera { XMFLOAT2  offset;    XMFLOAT2 viewSize; };

// 축 정렬 경계 박스 - 충돌 판정에 사용
struct AABB
{
    float left, right, bottom, top;
    bool Overlaps(const AABB& o) const
    {
        return left < o.right && right > o.left &&
            bottom < o.top && top > o.bottom;
    }
};

// 플랫폼 타입 열거형
// 새 타입 추가 시: 1.여기에 추가 2.AddPlatform switch에 기본색 추가 3.UpdateMovement 분기에 동작 추가
enum class PlatformType {
    Normal,
    Ice,
    PassThrough,
    Vanishing,
    Reverse,
    Moving,
    Bomb,
    Wall,
    Background
};

// 아이템 타입 열거형
// 새 아이템 추가 시: 여기에 추가 후 RouletteState, PlayerItemState도 함께 수정
enum class ItemType { None = 0, Fly, PassThrough, Shield, Checkpoint };

// 컴파일된 셰이더 묶음
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

// 프레임 간 경과 시간 계산
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
        return min(dt, 0.05f); // 최대 dt 제한으로 물리 튐 방지
    }
};

// Win32 창 생성 및 관리
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
            L"Jump Game  [Space:Jump  Arrow:Move  F:Fly  R:Reset  LClick:Coord]",
            WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
            rc.right - rc.left, rc.bottom - rc.top,
            NULL, NULL, hInst, NULL);
        if (!hWnd) return false;
        ShowWindow(hWnd, SW_SHOW);
        return true;
    }
    ~WindowContext() { UnregisterClass(L"JumpGame", GetModuleHandle(NULL)); }
};

// DirectX 11 디바이스, 컨텍스트, 스왑체인 관리
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

        // 포인트 샘플러 - 픽셀 아트 스타일 유지
        D3D11_SAMPLER_DESC smpDesc = {};
        smpDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
        smpDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
        smpDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
        smpDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
        Device->CreateSamplerState(&smpDesc, &Sampler);

        // 알파 블렌딩 설정
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

    // HLSL 소스 문자열을 받아 VS, PS, InputLayout을 한 번에 컴파일
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

// WIC를 이용한 PNG 텍스처 로드
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
        printf("[Texture] file load failed: %ls\n", path);
        factory->Release();
        return nullptr;
    }

    decoder->GetFrame(0, &frame);
    factory->CreateFormatConverter(&converter);
    // 모든 포맷을 RGBA32로 통일
    converter->Initialize(frame, GUID_WICPixelFormat32bppRGBA,
        WICBitmapDitherTypeNone, nullptr, 0.0,
        WICBitmapPaletteTypeCustom);

    UINT w = 0, h = 0;
    converter->GetSize(&w, &h);
    std::vector<BYTE> pixels(w * h * 4);
    converter->CopyPixels(nullptr, w * 4, (UINT)pixels.size(), pixels.data());

    D3D11_TEXTURE2D_DESC td = {};
    td.Width = w; td.Height = h;
    td.MipLevels = 1; td.ArraySize = 1;
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

    printf("[Texture] load sucess: %ls (%ux%u)\n", path, w, h);
    return srv;
}

// 같은 경로의 텍스처를 중복 로드하지 않도록 캐싱
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

// GPU 정점 버퍼 래퍼
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

    // [-0.5, 0.5] 기준 단위 사각형 생성
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

// 렌더링 재질 기본 클래스
class Material
{
public:
    ShaderSet Shaders;
    explicit Material(ShaderSet s) : Shaders(s) {}
    virtual ~Material() {}
    virtual void Bind(GraphicsContext* gfx) = 0;
};

// 단색 또는 텍스처 재질
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

    // 텍스처 설정 시 색상은 흰색으로 초기화 (텍스처 색상 그대로 표시)
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

// 컴포넌트 기본 클래스 - 게임 오브젝트에 부착되어 동작을 구현
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

// 씬에 존재하는 모든 오브젝트의 기본 단위
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

// 메쉬와 재질을 이용해 게임 오브젝트를 화면에 그리는 컴포넌트
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

        // SRT 순서로 월드 변환 행렬 구성
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

// 점프킹 스타일 카메라
// X축 고정, Y축은 한 화면 단위로 컷 전환, Y는 StartY 미만으로 내려가지 않음
class Camera
{
public:
    Vec2          Pos = { 0, 0 };
    float         ViewW = 0;
    float         ViewH = 0;
    ID3D11Buffer* CB = nullptr;

    float StartY = 0.0f;   // 카메라 Y 최솟값 (맵 바닥)
    float FixedX = 0.0f;   // 카메라 X 고정값

    void Init(GraphicsContext* gfx, int w, int h)
    {
        ViewW = (float)w; ViewH = (float)h;
        D3D11_BUFFER_DESC bd = {};
        bd.Usage = D3D11_USAGE_DEFAULT;
        bd.ByteWidth = sizeof(CbCamera);
        bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        gfx->Device->CreateBuffer(&bd, nullptr, &CB);
    }

    // dt 매개변수는 현재 미사용 - 부드러운 보간 제거 후 시그니처 유지용으로만 남아 있음
    void Follow(Vec2 target, float dt)
    {
        Pos.x = FixedX;

        // 셰이더 좌표 변환 기준 한 화면의 월드 높이
        const float SCREEN_WORLD_H = ViewH / 200.0f;
        const float halfView = SCREEN_WORLD_H * 0.5f;

        // 플레이어가 화면 상단을 벗어나면 한 화면 위로 이동
        while (target.y > Pos.y + halfView)
            Pos.y += SCREEN_WORLD_H;

        // 플레이어가 화면 하단을 벗어나면 한 화면 아래로 이동 (바닥 제한 있음)
        while (target.y < Pos.y - halfView && Pos.y > StartY)
            Pos.y -= SCREEN_WORLD_H;

        if (Pos.y < StartY) Pos.y = StartY;
    }

    void Upload(GraphicsContext* gfx)
    {
        CbCamera cb = { {Pos.x, Pos.y}, {ViewW, ViewH} };
        gfx->Context->UpdateSubresource(CB, 0, nullptr, &cb, 0, 0);
        gfx->Context->VSSetConstantBuffers(2, 1, &CB);
    }

    // 마우스 클릭 위치를 월드 좌표로 변환
    Vec2 ScreenToWorld(int sx, int sy) const
    {
        float nx = (float)sx / ViewW * 2.0f - 1.0f;
        float ny = 1.0f - (float)sy / ViewH * 2.0f;
        float hw = ViewW / 200.0f, hh = ViewH / 200.0f;
        return { Pos.x + nx * hw, Pos.y + ny * hh };
    }

    ~Camera() { if (CB) CB->Release(); }
};

// 플랫폼 타입별 동작과 충돌 속성을 담당하는 컴포넌트
class PlatformComp : public Component
{
public:
    PlatformType Type;

    Vec2  StartPos = { 0, 0 };   // Moving 플랫폼 기준 위치
    float MoveTimer = 0.0f;
    float MoveRange = 0.6f;
    float MoveSpeed = 1.5f;

    bool  IsActive = true;

    // Vanishing 플랫폼 전용
    float Timer = 0.0f;
    bool  Triggered = false;   // 플레이어가 한 번 밟았는지

    // Bomb 플랫폼 전용
    bool  IsShaking = false;
    float BombTimer = 0.0f;
    float IdleTime = 2.0f;   // 폭발 전 대기 시간
    float ShakeTime = 0.7f;   // 흔들림 지속 시간
    float OriginX = 0.0f;
    bool  HasBouncedPlayer = false;  // 이번 폭발에서 이미 플레이어를 튕겼는지

    PlatformComp(PlatformType t) : Type(t) {}

    void Start(GraphicsContext* gfx) override
    {
        StartPos = Owner->Pos;
        OriginX = Owner->Pos.x;
    }

    void Update(float dt) override
    {
        if (Type == PlatformType::Vanishing)
        {
            if (Triggered)
            {
                Timer += dt;
                // 3초 후 비활성화
                if (Timer >= 3.0f && IsActive)
                {
                    IsActive = false;
                    Owner->Visible = false;
                }
                // 5초 후 재생성
                if (Timer >= 5.0f)
                {
                    IsActive = true;
                    Owner->Visible = true;
                    Triggered = false;
                    Timer = 0.0f;
                }
            }
        }

        // Moving 플랫폼: 사인 함수로 좌우 왕복
        if (Type == PlatformType::Moving)
        {
            MoveTimer += dt * MoveSpeed;
            Owner->Pos.x = StartPos.x + sinf(MoveTimer) * MoveRange;
        }

        // Bomb 플랫폼: 대기 후 흔들림, 흔들림 종료 후 리셋
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
                // 고주파 진동으로 흔들림 표현
                Owner->Pos.x = OriginX + sinf(BombTimer * 50.0f) * 0.08f;

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

// 플레이어 아이템 효과 상태 관리
// PlayerController 전방 선언이 필요하므로 구조체 정의만 먼저 배치, 함수 구현은 PlayerController 정의 이후
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

// 룰렛 상태 및 진행 관리
// 새 아이템 추가 시 Items 배열과 ITEM_COUNT를 함께 수정
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
    float FlipTimer = 0.0f;   // 다음 슬롯 전환까지 남은 시간
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

    // 매 프레임 호출. 룰렛이 완전히 끝나면 true 반환
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
            // 진행률에 따라 슬롯 전환 간격을 점점 늘려서 속도감 표현
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

    // 결과 표시 시 아이콘 크기를 사인 곡선으로 펄스 효과
    float ShowResultScale() const
    {
        float t = 1.0f - ShowResultTimer / SHOW_RESULT_DURATION;
        return 1.0f + 0.5f * sinf(t * 3.14159f);
    }
};

// 플레이어 이동, 점프, 충돌, 아이템, 룰렛을 처리하는 핵심 컴포넌트
class PlayerController : public Component
{
public:
    // 외부 주입 참조
    std::vector<GameObject*>* Platforms = nullptr;
    PlayerItemState* ItemState = nullptr;
    GameObject* CheckpointFlag = nullptr;

    // 물리 상태
    Vec2  Vel = { 0, 0 };
    bool  OnGround = false;
    float HalfW = 0.3f;
    float HalfH = 0.3f;

    Vec2 PrevPos = { 0, 0 };  // 이전 프레임 위치 - 충돌 방향 판별에 사용

    // 점프 충전 시스템
    bool  PrevSpace = false;
    bool  SpacePressedThisFrame = false;
    bool  SpaceHeld = false;
    float JumpCharge = 0.0f;  // 0~1 범위, 릴리즈 시 점프 속도에 반영
    bool  JumpedThisPress = false;

    // 조작감 보조 타이머
    float CoyoteTimer = 0.0f;  // 낙하 직후 일정 시간 점프 허용
    float JumpBufferTimer = 0.0f;  // 착지 직전 점프 입력을 착지 후에도 유효하게 처리
    float CurrentShakeX = 0.0f; // 현재 미사용 - 점프 충전 중 시각적 흔들림용으로 계산은 하나 적용 안 함

    bool  IsStunned = false;

    float MoveInput = 0.0f;

    // 지면 타입 플래그
    bool  OnIce = false;
    float ReverseTimer = 0.0f;  // 0보다 크면 조작 반전 상태
    bool IsReverse() const { return ReverseTimer > 0.0f; }

    // 벽 및 천장 충돌 시 수평 튕김 설정
    float BounceFactor = 0.85f;
    float MinBounceSpeed = 3.0f;
    float MaxBounceSpeed = 6.0f;

    bool FlyMode = false;  // F키 개발자 비행
    bool item_flymode = false;  // 아이템 비행 (속도가 다름)

    Vec2 Checkpoint = { 0, 0.5f };

    RouletteState Roulette;

    // 아이템 상태별 플레이어 외형 재질 - 외부에서 주입
    ColorMaterial* MatNormal = nullptr;
    ColorMaterial* MatFly = nullptr;
    ColorMaterial* MatPassThrough = nullptr;
    ColorMaterial* MatShield = nullptr;

    bool PrevF = false, PrevR = false;

    void Input() override
    {
        bool spaceNow = (GetAsyncKeyState(VK_SPACE) & 0x8000) != 0;
        SpacePressedThisFrame = spaceNow && !PrevSpace;
        SpaceHeld = spaceNow;
        PrevSpace = spaceNow;

        bool fNow = (GetAsyncKeyState('F') & 0x8000) != 0;
        bool rNow = (GetAsyncKeyState('R') & 0x8000) != 0;

        // 디버그용 아이템 즉시 발동 단축키
        if (GetAsyncKeyState('1') & 0x8000) ItemState->Apply(ItemType::Fly, this);
        if (GetAsyncKeyState('2') & 0x8000) ItemState->Apply(ItemType::PassThrough, this);
        if (GetAsyncKeyState('3') & 0x8000) ItemState->Apply(ItemType::Shield, this);
        if (GetAsyncKeyState('4') & 0x8000) ItemState->Apply(ItemType::Checkpoint, this);

        MoveInput = 0.0f;
        if (GetAsyncKeyState(VK_LEFT) & 0x8000) MoveInput -= 1.0f;
        if (GetAsyncKeyState(VK_RIGHT) & 0x8000) MoveInput += 1.0f;

        // F키: 개발자 비행 토글
        if (fNow && !PrevF)
        {
            FlyMode = !FlyMode; Vel = { 0, 0 };
            printf("[Dev] FlyMode: %s\n", FlyMode ? "ON" : "OFF");
        }
        // R키: 체크포인트로 리셋 (깃발이 설치된 경우에만)
        if (rNow && !PrevR)
        {
            if (CheckpointFlag->Active == false) return;
            if (CheckpointFlag) CheckpointFlag->Active = false;
            Owner->Pos = Checkpoint; Vel = { 0, 0 };
            printf("[Dev] Reset (%.2f, %.2f)\n", Checkpoint.x, Checkpoint.y);
        }

        PrevF = fNow; PrevR = rNow;
    }

    void Update(float dt) override
    {
        if (ItemState) ItemState->Update(dt, this);

        // 룰렛 종료 시 결과 아이템 발동
        if (Roulette.IsRunning())
        {
            bool done = Roulette.Update(dt);
            if (done && ItemState)
            {
                ItemType result = Roulette.GetResult();
                printf("[RandomBox] Item: type=%d -> activated\n", (int)result);
                ItemState->Apply(result, this);
            }
        }

        // 아이템 상태에 따라 플레이어 외형 재질 전환
        if (MatNormal)
        {
            auto* mr = Owner->GetComponent<MeshRenderer>();
            if (mr)
            {
                if (ItemState && ItemState->FlyActive)         mr->pMat = MatFly;
                else if (ItemState && ItemState->PassThroughActive) mr->pMat = MatPassThrough;
                else if (ItemState && ItemState->ShieldActive)      mr->pMat = MatShield;
                else                                                mr->pMat = MatNormal;
            }
        }

        PrevPos = Owner->Pos;

        const float WALL_RIGHT = 5.0f;
        const float WALL_LEFT = -5.0f;

        if (ItemState && ItemState->FlyActive) FlyMode = true;

        if (FlyMode)
        {
            UpdateFly(dt);
            if (item_flymode) ResolveAllCollisions(dt);
            Owner->Rot += (0.0f - Owner->Rot) * 15.0f * dt;
            // 비행 중 맵 경계 클램프
            if (Owner->Pos.x + HalfW > WALL_RIGHT) Owner->Pos.x = WALL_RIGHT - HalfW;
            else if (Owner->Pos.x - HalfW < WALL_LEFT) Owner->Pos.x = WALL_LEFT + HalfW;
            if (Owner->Pos.y < 0.3)   Owner->Pos.y = 0.3f;
            if (Owner->Pos.y > 53.0f) Owner->Pos.y = 53.0f;
            return;
        }

        Vel.y += GRAVITY * dt;

        if (ReverseTimer > 0.0f) ReverseTimer -= dt;

        UpdateMovement(dt);
        UpdateJump(dt);

        if (Owner->Pos.y > 53.0f) Owner->Pos.y = 53.0f;

        // 오른쪽 벽 충돌 후 튕김
        if (Owner->Pos.x + HalfW > WALL_RIGHT)
        {
            Owner->Pos.x = WALL_RIGHT - HalfW;
            if (Vel.x > 0.0f)
            {
                float speed = max(MinBounceSpeed, Vel.x);
                float bouncedSpeed = min(speed * 1.3, MaxBounceSpeed);
                Vel.x = -bouncedSpeed;
                if (Vel.y > 0.0f) Vel.y *= 0.6f;
            }
        }
        // 왼쪽 벽 충돌 후 튕김
        else if (Owner->Pos.x - HalfW < WALL_LEFT)
        {
            Owner->Pos.x = WALL_LEFT + HalfW;
            if (Vel.x < 0.0f)
            {
                float speed = max(MinBounceSpeed, -Vel.x);
                float bouncedSpeed = min(speed * 1.3, MaxBounceSpeed);
                Vel.x = bouncedSpeed;
                if (Vel.y > 0.0f) Vel.y *= 0.6f;
            }
        }

        Owner->Pos.x += Vel.x * dt;
        Owner->Pos.y += Vel.y * dt;

        ResolveAllCollisions(dt);

        // 점프 충전 중 흔들림 강도 계산 (CurrentShakeX는 현재 미사용)
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

        // 공중에서 기울기, 착지 시 회전 초기화
        if (!OnGround) Owner->Rot -= Vel.x * 3.0f * dt;
        else           Owner->Rot += (0.0f - Owner->Rot) * 15.0f * dt;
    }

private:
    void UpdateMovement(float dt)
    {
        // 점프 충전 중에는 이동 차단 (타입 무관 공통 처리)
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
                // Reverse 플랫폼: 입력 방향 반전
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

    void UpdateJump(float dt)
    {
        // 코요테 타임: 낙하 직후 0.1초간 점프 허용
        if (OnGround)       CoyoteTimer = 0.1f;
        else if (CoyoteTimer > 0.0f) CoyoteTimer -= dt;

        // 점프 버퍼: 착지 직전 0.15초 이내 입력을 착지 후 즉시 실행
        if (SpacePressedThisFrame) JumpBufferTimer = 0.15f;
        else if (JumpBufferTimer > 0.0f) JumpBufferTimer -= dt;

        bool canJump = (OnGround || CoyoteTimer > 0.0f) && !IsStunned;

        if (canJump)
        {
            if ((SpaceHeld || JumpBufferTimer > 0.0f) && !JumpedThisPress)
                JumpCharge = min(JumpCharge + JUMP_CHARGE * dt, 1.0f);

            // 스페이스 릴리즈 시 충전량에 비례한 속도로 점프
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
            // 공중에서는 충전값 즉시 초기화
            JumpCharge = 0.0f;
            if (!SpaceHeld) JumpedThisPress = false;
        }
    }

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
        if (plat->Type == PlatformType::Background) return;

        AABB player = GetAABB();
        AABB platform = plat->GetAABB();

        if (!plat->IsActive)         return;
        if (!player.Overlaps(platform)) return;

        bool shielded = (ItemState && ItemState->ShieldActive);

        // 최소 침투 방향 계산
        float oL = player.right - platform.left;
        float oR = platform.right - player.left;
        float oB = player.top - platform.bottom;
        float oT = platform.top - player.bottom;
        float minO = min(min(oL, oR), min(oB, oT));

        float prevBottom = PrevPos.y - HalfH;
        bool  fromAbove = (prevBottom >= platform.top - 0.01f);

        // PassThrough 타입이거나, 아이템 관통 중이면서 Wall이 아닌 경우
        if (plat->Type == PlatformType::PassThrough ||
            (ItemState && ItemState->PassThroughActive && plat->Type != PlatformType::Wall))
        {
            // 위에서 내려오는 경우에만 착지 처리
            if (minO == oT && Vel.y <= 0.0f && fromAbove)
            {
                Owner->Pos.y = platform.top + HalfH;
                Vel.y = 0;
                OnGround = true;
            }

            // 아이템 관통 중일 때 특수 플랫폼 효과 적용
            if (ItemState && ItemState->PassThroughActive)
            {
                if (shielded) { ReverseTimer = 0.0f; return; }

                if (plat->Type == PlatformType::Ice)     OnIce = true;
                if (plat->Type == PlatformType::Reverse) ReverseTimer = 2.0f;
                else                                     ReverseTimer = 0.0f;
                if (plat->Type == PlatformType::Vanishing) plat->Triggered = true;

                if (plat->Type == PlatformType::Bomb && plat->IsShaking && !plat->HasBouncedPlayer)
                {
                    plat->HasBouncedPlayer = true;
                    Vel.y = 12.0f;
                    float dir = (rand() % 2 == 0) ? -1.0f : 1.0f;
                    Vel.x = dir * 6.0f;
                    OnGround = false;
                }
            }
            return;
        }

        const float HORIZONTAL_BOUNCE = 1.3f;

        // 측면 충돌 판정: 수평 침투가 최소이거나 수평 속도가 있고 수직 최소가 아닌 경우
        bool hitSideWall = (minO == oL || minO == oR) ||
            (abs(Vel.x) > 1.0f && minO != oT && minO != oB);

        if (hitSideWall && !OnGround)
        {
            if (oL < oR)  // 왼쪽 면에 박힌 경우 -> 왼쪽으로 밀어내고 반대 방향 튕김
            {
                Owner->Pos.x = platform.left - HalfW;
                if (Vel.x >= 0.0f)
                {
                    float speed = max(MinBounceSpeed, Vel.x);
                    float bouncedSpeed = speed * HORIZONTAL_BOUNCE;
                    if (bouncedSpeed > MaxBounceSpeed) bouncedSpeed = MaxBounceSpeed;
                    Vel.x = -bouncedSpeed;
                    if (Vel.y > 0.0f) Vel.y *= 0.6f;
                }
            }
            else  // 오른쪽 면에 박힌 경우 -> 오른쪽으로 밀어내고 반대 방향 튕김
            {
                Owner->Pos.x = platform.right + HalfW;
                if (Vel.x <= 0.0f)
                {
                    float speed = max(MinBounceSpeed, -Vel.x);
                    float bouncedSpeed = speed * HORIZONTAL_BOUNCE;
                    if (bouncedSpeed > MaxBounceSpeed) bouncedSpeed = MaxBounceSpeed;
                    Vel.x = bouncedSpeed;
                    if (Vel.y > 0.0f) Vel.y *= 0.6f;
                }
            }
        }
        else
        {
            if (minO == oT)  // 위에서 착지
            {
                if (Vel.y <= 0.0f)
                {
                    Owner->Pos.y = platform.top + HalfH;
                    Vel.y = 0;
                    OnGround = true;

                    if (shielded) { ReverseTimer = 0.0f; return; }

                    if (plat->Type == PlatformType::Ice)     OnIce = true;
                    if (plat->Type == PlatformType::Reverse) ReverseTimer = 2.0f;
                    else                                     ReverseTimer = 0.0f;
                    if (plat->Type == PlatformType::Vanishing) plat->Triggered = true;

                    if (plat->Type == PlatformType::Bomb && plat->IsShaking && !plat->HasBouncedPlayer)
                    {
                        plat->HasBouncedPlayer = true;
                        Vel.y = 12.0f;
                        float dir = (rand() % 2 == 0) ? -1.0f : 1.0f;
                        Vel.x = dir * 6.0f;
                        OnGround = false;
                    }
                }
            }
            else if (minO == oB)  // 아래에서 천장에 부딪힘
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

        if (item_flymode)
        {
            Owner->Pos.x += dir.x * FLY_ITEM_SPEED * dt;
            Owner->Pos.y += dir.y * FLY_ITEM_SPEED * dt;
        }
        else
        {
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

// PlayerItemState 함수 구현 - PlayerController 정의 이후 배치
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
        printf("[Item] Fly activated %.1fs\n", ITEM_FLY_DURATION);
        break;

    case ItemType::PassThrough:
        PassThroughActive = true;
        PassThroughTimer = ITEM_PASSTHROUGH_DURATION;
        printf("[Item] PassThrough activated %.1fs\n", ITEM_PASSTHROUGH_DURATION);
        break;

    case ItemType::Shield:
        ShieldActive = true;
        ShieldTimer = ITEM_SHIELD_DURATION;
        printf("[Item] Shield activated %.1fs\n", ITEM_SHIELD_DURATION);
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
        printf("[Item] Checkpoint installation (%.2f, %.2f)\n", CheckpointPos.x, CheckpointPos.y);
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
            printf("[Item] Fly end\n");
        }
    }
    if (PassThroughActive)
    {
        PassThroughTimer -= dt;
        if (PassThroughTimer <= 0.0f)
        {
            PassThroughActive = false;
            PassThroughTimer = 0.0f;
            printf("[Item] PassThrough end\n");
        }
    }
    if (ShieldActive)
    {
        ShieldTimer -= dt;
        if (ShieldTimer <= 0.0f)
        {
            ShieldActive = false;
            ShieldTimer = 0.0f;
            printf("[Item] Shield end\n");
        }
    }
}

// 점프 충전량을 플레이어 머리 위 바 형태로 시각화
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

        // 충전량에 비례해 바 너비 변화
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

// 발동 중인 아이템 잔여 시간을 플레이어 위에 색상 바로 표시
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
        // 바가 왼쪽 기준으로 줄어들도록 중심 위치 보정
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

// 랜덤박스 획득 시 플레이어 머리 위에 아이템 아이콘을 스핀하며 표시
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
        // 결과 표시 중에는 펄스 크기 적용
        float scl = pc->Roulette.ShowResult ? pc->Roulette.ShowResultScale() : 1.0f;
        auto* mat = (idx >= 0 && idx < RouletteState::ITEM_COUNT) ? MatIcon[idx] : nullptr;
        if (!mat || !pQuad || !CB) return;

        mat->Bind(gfx);
        float    size = IconSize * scl;
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

// 체크포인트 깃대(세로 막대)와 깃발(사각형)을 렌더링
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

// 플레이어가 접촉하면 사라지고 룰렛을 시작하는 랜덤박스 컴포넌트
class RandomBoxComp : public Component
{
public:
    bool              Picked = false;
    GameObject* Player = nullptr;
    PlayerController* PC = nullptr;

    float RespawnTimer = 0.0f;
    static constexpr float RESPAWN_DURATION = 30.0f;  // 획득 후 재생성까지 걸리는 시간

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
                printf("[RandomBox] regenerated!\n");
            }
            return;
        }

        if (!Player || !PC) return;

        // 플레이어와의 거리 판정
        float dx = Owner->Pos.x - Player->Pos.x;
        float dy = Owner->Pos.y - Player->Pos.y;
        if (sqrtf(dx * dx + dy * dy) < ITEM_PICKUP_RADIUS
            && !PC->Roulette.Active && !PC->Roulette.ShowResult)
        {
            Picked = true;
            RespawnTimer = 0.0f;
            Owner->Visible = false;
            PC->Roulette.Start();
            printf("[RandomBox] Start roulette!\n");
        }
    }
};

// 플레이어가 골 영역에 닿으면 클리어 상태를 true로 설정
class GoalComp : public Component
{
public:
    GameObject* Player = nullptr;
    bool* Cleared = nullptr;

    AABB GetAABB() const
    {
        float hw = Owner->Scale.x * 0.5f;
        float hh = Owner->Scale.y * 0.5f;
        return { Owner->Pos.x - hw, Owner->Pos.x + hw,
                 Owner->Pos.y - hh, Owner->Pos.y + hh };
    }

    void Update(float dt) override
    {
        if (!Player || !Cleared || *Cleared) return;

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

// 클리어 시 화면 중앙에 MISSION COMPLETE 배너를 표시
// 카메라 위치에 그리므로 화면 이동과 무관하게 항상 중앙에 위치
class MissionBanner : public Component
{
    ID3D11Buffer* CB = nullptr;
public:
    Camera* Cam = nullptr;
    bool* Cleared = nullptr;
    ColorMaterial* Mat = nullptr;
    Mesh* Quad = nullptr;
    float          Width = 10.0f;
    float          Height = 2.5f;

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

// 게임 전체 루프 및 씬 관리
class GameLoop
{
public:
    WindowContext            Win;
    GraphicsContext          Gfx;
    DeltaTime                Timer;
    Camera                   Cam;
    TextureCache             TexCache;
    bool                     Running = true;
    bool                     Cleared = false;

    std::vector<GameObject*> World;        // 플레이어, 골 등 주요 오브젝트
    std::vector<GameObject*> Platforms;
    std::vector<GameObject*> RandomBoxes;

    GameObject* FlagObject = nullptr;
    PlayerItemState  ItemStateData;

    ShaderSet      DefaultShaders;
    Mesh* QuadMesh = nullptr;  // 단위 사각형 [-0.5, 0.5]
    Mesh* BarMesh = nullptr;  // 바 전용 사각형 [0, 1]

    ColorMaterial* MatPlayer = nullptr;
    ColorMaterial* MatPlayerFly = nullptr;
    ColorMaterial* MatPlayerPT = nullptr;
    ColorMaterial* MatPlayerShld = nullptr;
    ColorMaterial* MatChargeBar = nullptr;

    static constexpr int ITEM_MAT_COUNT = 4;
    ColorMaterial* MatItemBar[ITEM_MAT_COUNT] = {};  // 아이템 잔여 시간 바
    ColorMaterial* MatRouletteIcon[ITEM_MAT_COUNT] = {};  // 룰렛 아이콘

    ColorMaterial* MatFlagMast = nullptr;
    ColorMaterial* MatFlagFlag = nullptr;

    std::vector<Material*> OwnedMaterials;  // 소유권 관리용 - 소멸 시 일괄 해제

    bool MousePressed = false;

    // 교대로 활성화되는 패턴 플랫폼 목록
    std::vector<PlatformComp*> PatternPlatforms;
    float PatternTimer = 0.0f;
    float PatternInterval = 1.5f;  // 교대 주기
    int   PatternIndex = 0;

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

    bool Initialize(HINSTANCE hInst,
        LRESULT(CALLBACK* proc)(HWND, UINT, WPARAM, LPARAM))
    {
        if (!Win.Initialize(hInst, proc))             return false;
        if (!Gfx.Init(Win.hWnd, SCREEN_W, SCREEN_H)) return false;

        Cam.Init(&Gfx, SCREEN_W, SCREEN_H);
        CoInitialize(nullptr);
        TexCache.Init(&Gfx);
        srand((unsigned)GetTickCount64());

        // HLSL 셰이더 소스
        // VS: 월드 변환 후 카메라 오프셋 기준으로 NDC 좌표 계산
        // PS: 텍스처 유무에 따라 샘플링 또는 단색 출력
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

        QuadMesh = new Mesh(); QuadMesh->CreateQuad(&Gfx, -0.5f, -0.5f, 0.5f, 0.5f);
        BarMesh = new Mesh(); BarMesh->CreateQuad(&Gfx, 0.0f, 0.0f, 1.0f, 1.0f);

        MatPlayer = new ColorMaterial(DefaultShaders, { 0.3f, 0.6f, 1.0f, 1.0f }, &Gfx);
        MatPlayerFly = new ColorMaterial(DefaultShaders, { 0.3f, 0.8f, 1.0f, 1.0f }, &Gfx);
        MatPlayerPT = new ColorMaterial(DefaultShaders, { 0.4f, 1.0f, 0.5f, 1.0f }, &Gfx);
        MatPlayerShld = new ColorMaterial(DefaultShaders, { 1.0f, 0.3f, 0.3f, 1.0f }, &Gfx);
        MatChargeBar = new ColorMaterial(DefaultShaders, { 1.0f, 0.9f, 0.0f, 1.0f }, &Gfx);

        // 텍스처가 없으면 기본 색상으로 표시
        auto* playerTex = TexCache.Get(L"player.png");
        if (playerTex) MatPlayer->SetTexture(playerTex);

        auto* srvFly = TexCache.Get(L"player_fly.png");
        auto* srvPT = TexCache.Get(L"player_passthrough.png");
        auto* srvShld = TexCache.Get(L"player_shield.png");
        if (srvFly)  MatPlayerFly->SetTexture(srvFly);
        if (srvPT)   MatPlayerPT->SetTexture(srvPT);
        if (srvShld) MatPlayerShld->SetTexture(srvShld);

        // 아이템별 색상 및 텍스처 설정
        XMFLOAT4 itemColors[ITEM_MAT_COUNT] = {
            { 0.3f, 0.8f, 1.0f, 1.0f },  // Fly - 하늘색
            { 0.4f, 1.0f, 0.5f, 1.0f },  // PassThrough - 초록
            { 1.0f, 0.3f, 0.3f, 1.0f },  // Shield - 빨강
            { 1.0f, 0.9f, 0.2f, 1.0f },  // Checkpoint - 노랑
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

        MatFlagMast = new ColorMaterial(DefaultShaders, { 0.7f, 0.5f, 0.2f, 1.0f }, &Gfx);
        MatFlagFlag = new ColorMaterial(DefaultShaders, { 1.0f, 0.2f, 0.2f, 1.0f }, &Gfx);
        MatFlagMast->SetTexture(TexCache.Get(L"flag_mast.png"));
        MatFlagFlag->SetTexture(TexCache.Get(L"flag_flag.png"));

        BuildMap();
        BuildRandomBoxes();
        BuildFlag();
        BuildPlayer();
        BuildGoal();

        // 플레이어 참조를 랜덤박스 컴포넌트에 주입
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
        printf("  F : Fly   R : Checkpoint Reset   ESC : Quit\n");
        printf("  LClick : Print world coord\n");
        printf("  [ITEM] Fly  PassThrough  Shield  Checkpoint\n");
        return true;
    }

    // 맵 플랫폼 배치
    // 플랫폼 추가/수정은 이 함수 내부에서만 진행
    void BuildMap()
    {
        // 왼쪽 벽
        AddPlatform(PlatformType::Wall, -10.0f, -5.0f, -5.0f, 0.0f, L"stoneWall.png");
        AddPlatform(PlatformType::Wall, -10.0f, 0.0f, -5.0f, 5.0f, L"stoneWall.png");
        AddPlatform(PlatformType::Wall, -10.0f, 5.0f, -5.0f, 10.0f, L"stoneWall.png");
        AddPlatform(PlatformType::Wall, -10.0f, 10.0f, -5.0f, 15.0f, L"stoneWall.png");
        AddPlatform(PlatformType::Wall, -10.0f, 15.0f, -5.0f, 20.0f, L"stoneWall.png");
        AddPlatform(PlatformType::Wall, -10.0f, 20.0f, -5.0f, 25.0f, L"stoneWall.png");
        AddPlatform(PlatformType::Wall, -10.0f, 25.0f, -5.0f, 30.0f, L"stoneWall.png");
        AddPlatform(PlatformType::Wall, -10.0f, 30.0f, -5.0f, 35.0f, L"stoneWall.png");
        AddPlatform(PlatformType::Wall, -10.0f, 35.0f, -5.0f, 40.0f, L"stoneWall.png");
        AddPlatform(PlatformType::Wall, -10.0f, 40.0f, -5.0f, 45.0f, L"stoneWall.png");
        AddPlatform(PlatformType::Wall, -10.0f, 45.0f, -5.0f, 50.0f, L"stoneWall.png");
        AddPlatform(PlatformType::Wall, -10.0f, 50.0f, -5.0f, 55.0f, L"stoneWall.png");
        AddPlatform(PlatformType::Wall, -10.0f, 55.0f, -5.0f, 60.0f, L"stoneWall.png");

        // 오른쪽 벽
        AddPlatform(PlatformType::Wall, 5.0f, -5.0f, 10.0f, 0.0f, L"stoneWall.png");
        AddPlatform(PlatformType::Wall, 5.0f, 0.0f, 10.0f, 5.0f, L"stoneWall.png");
        AddPlatform(PlatformType::Wall, 5.0f, 5.0f, 10.0f, 10.0f, L"stoneWall.png");
        AddPlatform(PlatformType::Wall, 5.0f, 10.0f, 10.0f, 15.0f, L"stoneWall.png");
        AddPlatform(PlatformType::Wall, 5.0f, 15.0f, 10.0f, 20.0f, L"stoneWall.png");
        AddPlatform(PlatformType::Wall, 5.0f, 20.0f, 10.0f, 25.0f, L"stoneWall.png");
        AddPlatform(PlatformType::Wall, 5.0f, 25.0f, 10.0f, 30.0f, L"stoneWall.png");
        AddPlatform(PlatformType::Wall, 5.0f, 30.0f, 10.0f, 35.0f, L"stoneWall.png");
        AddPlatform(PlatformType::Wall, 5.0f, 35.0f, 10.0f, 40.0f, L"stoneWall.png");
        AddPlatform(PlatformType::Wall, 5.0f, 40.0f, 10.0f, 45.0f, L"stoneWall.png");
        AddPlatform(PlatformType::Wall, 5.0f, 45.0f, 10.0f, 50.0f, L"stoneWall.png");
        AddPlatform(PlatformType::Wall, 5.0f, 50.0f, 10.0f, 55.0f, L"stoneWall.png");
        AddPlatform(PlatformType::Wall, 5.0f, 55.0f, 10.0f, 60.0f, L"stoneWall.png");

        // 배경 (충돌 없음)
        AddPlatform(PlatformType::Background, -5.0f, 0.0f, 5.0f, 20.0f, L"background1__.png");
        AddPlatform(PlatformType::Background, -5.0f, 20.0f, 5.0f, 40.0f, L"background2_.png");
        AddPlatform(PlatformType::Background, -5.0f, 40.0f, 5.0f, 60.0f, L"background3_.png");

        // 바닥
        AddPlatform(PlatformType::Normal, -5.0f, -5.0f, 5.0f, 0.0f, L"ground2.png");

        // 1층
        AddPlatform(PlatformType::Normal, -3.0f, 1.5f, 0.0f, 1.8f, L"ground1.png");
        AddPlatform(PlatformType::Ice, 1.0f, 1.5f, 3.5f, 1.8f, L"ice1.png");
        AddPlatform(PlatformType::Normal, -1.0f, 3.5f, 2.0f, 3.8f, L"ground1.png");

        // 2층
        AddPlatform(PlatformType::Normal, -4.0f, 5.0f, -1.5f, 5.3f, L"ground1.png");
        AddPlatform(PlatformType::Ice, 0.0f, 5.5f, 3.0f, 5.8f, L"ice1.png");
        AddPlatform(PlatformType::Normal, -2.0f, 7.0f, 0.5f, 7.3f, L"ground1.png");
        AddPlatform(PlatformType::Normal, 1.5f, 7.0f, 4.0f, 7.3f, L"ground1.png");

        // 3층
        AddPlatform(PlatformType::Normal, -3.5f, 9.0f, -2.0f, 9.2f, L"ground1.png");
        AddPlatform(PlatformType::Ice, -0.5f, 9.5f, 1.5f, 9.7f, L"ice1.png");
        AddPlatform(PlatformType::Normal, 2.5f, 10.0f, 4.5f, 10.2f, L"ground1.png");
        AddPlatform(PlatformType::Vanishing, -1.0f, 11.5f, 1.5f, 11.7f, L"vanishing1.png");

        // 4층
        AddPlatform(PlatformType::Reverse, -4.0f, 13.0f, -1.0f, 13.3f, L"reverse1.png");
        AddPlatform(PlatformType::Ice, 0.5f, 13.5f, 3.5f, 13.8f, L"ice1.png");
        AddPlatform(PlatformType::Moving, -2.0f, 15.5f, 2.0f, 15.8f, L"moving1.png");

        // 5층
        AddPlatform(PlatformType::Normal, -4.5f, 17.0f, -2.0f, 17.3f, L"ground1.png");
        AddPlatform(PlatformType::Ice, 2.0f, 17.3f, 4.5f, 17.6f, L"ice1.png");

        // 6층
        AddPlatform(PlatformType::Normal, -3.0f, 19.0f, -1.0f, 19.3f, L"ground1.png");
        AddPlatform(PlatformType::Normal, 1.0f, 19.5f, 3.0f, 19.8f, L"ground1.png");

        // 7층 - 패턴 플랫폼 (일정 간격으로 활성/비활성 교대)
        AddPatternPlatform(PlatformType::Normal, -4.5f, 20.5f, -2.5f, 20.8f, L"ground1.png");
        AddPatternPlatform(PlatformType::Ice, -1.0f, 21.4f, 1.0f, 21.7f, L"ice1.png");
        AddPatternPlatform(PlatformType::Reverse, 2.0f, 22.4f, 4.0f, 22.7f, L"reverse1.png");
        AddPatternPlatform(PlatformType::Normal, 0.0f, 23.4f, 2.0f, 23.7f, L"ground1.png");
        AddPatternPlatform(PlatformType::Ice, -3.0f, 24.4f, -1.0f, 24.7f, L"ice1.png");

        // 8층
        AddPlatform(PlatformType::Bomb, -4.5f, 26.0f, -2.5f, 26.3f, L"ground1.png");
        AddPlatform(PlatformType::Moving, -0.5f, 26.5f, 1.5f, 26.8f, L"moving1.png");

        // 9층
        AddPlatform(PlatformType::Ice, -3.0f, 29.1f, -0.5f, 29.4f, L"ice1.png");
        AddPlatform(PlatformType::Reverse, 1.5f, 28.5f, 4.0f, 28.8f, L"reverse1.png");

        // 10층
        AddPlatform(PlatformType::Bomb, 1.0f, 30.3f, 2.0f, 30.6f, L"ground1.png");
        AddPlatform(PlatformType::Normal, 3.0f, 31.3f, 4.0f, 31.6f, L"ground1.png");

        // 11층
        AddPlatform(PlatformType::Normal, 1.0f, 32.3f, 2.0f, 32.6f, L"ground1.png");
        AddPlatform(PlatformType::Normal, 3.0f, 33.3f, 4.0f, 33.6f, L"ground1.png");

        // 12층
        AddPlatform(PlatformType::Normal, 1.0f, 34.3f, 2.0f, 34.6f, L"ground1.png");
        AddPlatform(PlatformType::Normal, -0.8f, 34.3f, -0.2f, 34.6f, L"ground1.png");
        AddPlatform(PlatformType::Normal, -2.8f, 34.3f, -2.2f, 34.6f, L"ground1.png");
        AddPlatform(PlatformType::Normal, -3.8f, 35.3f, -3.2f, 35.6f, L"ground1.png");

        // 13층
        AddPlatform(PlatformType::Vanishing, -3.8f, 37.0f, -3.2f, 37.3f, L"vanishing2.png");
        AddPlatform(PlatformType::Normal, -1.1f, 37.0f, -0.1f, 37.3f, L"ground1.png");
        AddPlatform(PlatformType::Ice, 1.8f, 37.0f, 2.2f, 37.3f, L"ice1.png");

        // 14층
        AddPlatform(PlatformType::Ice, 3.8f, 38.5f, 4.2f, 38.8f, L"ice1.png");
        AddPlatform(PlatformType::Ice, 1.8f, 40.0f, 2.2f, 40.3f, L"ice1.png");
        AddPlatform(PlatformType::Ice, 3.4f, 41.5f, 4.2f, 41.8f, L"ice1.png");

        // 15층
        AddPlatform(PlatformType::Moving, -1.0f, 42.5f, 1.2f, 42.8f, L"moving1.png");
        AddPlatform(PlatformType::Moving, -3.0f, 44.3f, -2.5f, 44.6f, L"moving2.png");
        AddPlatform(PlatformType::Moving, -1.0f, 46.3f, -0.5f, 46.6f, L"moving2.png");
        AddPlatform(PlatformType::Moving, -3.0f, 48.3f, -2.5f, 48.6f, L"moving2.png");

        // 패턴 플랫폼 초기 활성화 (첫 두 개만 켜둠)
        if (PatternPlatforms.size() >= 2)
        {
            PatternPlatforms[0]->IsActive = true;
            PatternPlatforms[0]->Owner->Visible = true;
            PatternPlatforms[1]->IsActive = true;
            PatternPlatforms[1]->Owner->Visible = true;
        }

        // 꼭대기 (골 진입 발판)
        AddPlatform(PlatformType::Normal, -1.5f, 49.6f, 1.5f, 49.9f, L"ground1.png");
    }

    // 단색 또는 텍스처 플랫폼 생성
    // 타입에 따라 기본 색상 자동 할당, texPath가 있으면 텍스처로 덮어씀
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
        case PlatformType::Wall:        color = { 1.0f, 1.0f, 0.2f, 1.0f }; break;
        case PlatformType::Background:  color = { 1.0f, 1.0f, 1.0f, 1.0f }; break;
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

    // 패턴 플랫폼 생성 - 기본적으로 비활성 상태로 생성되며 PatternPlatforms에 등록됨
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
        case PlatformType::Wall:        color = { 1.0f, 1.0f, 0.2f, 1.0f }; break;
        case PlatformType::Background:  color = { 1.0f, 1.0f, 1.0f, 1.0f }; break;
        }

        auto* mat = new ColorMaterial(DefaultShaders, color, &Gfx);
        OwnedMaterials.push_back(mat);

        auto* srv = TexCache.Get(texPath);
        if (srv) mat->SetTexture(srv);

        auto* go = new GameObject(cx, cy);
        go->Scale = { w, h };
        go->AddComponent(new MeshRenderer(QuadMesh, mat));

        auto* plat = new PlatformComp(type);
        plat->IsActive = false;   // 기본 비활성
        go->Visible = false;

        go->AddComponent(plat);
        Platforms.push_back(go);
        PatternPlatforms.push_back(plat);
    }

    // 랜덤박스 오브젝트 배치
    void BuildRandomBoxes()
    {
        AddRandomBox(-2.0f, 29.6f);
        AddRandomBox(1.0f, 14.0f);
        AddRandomBox(-0.5f, 37.5f);
        AddRandomBox(2.0f, 20.0f);
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

    // 체크포인트 깃발 오브젝트 초기 생성 (기본 비활성 상태)
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

    // 플레이어 오브젝트 조립 및 초기 위치 설정
    void BuildPlayer()
    {
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

        // 카메라 초기화 - X 고정, Y 최솟값 = 시작 위치
        Cam.FixedX = startX;
        Cam.StartY = startY;
        Cam.Pos = { startX, startY };
    }

    // 클리어 지점 오브젝트 생성
    // 골 좌표와 크기만 수정하면 클리어 판정 위치가 바뀜
    void BuildGoal()
    {
        if (World.empty()) return;
        GameObject* player = World[0];

        auto* matGoal = new ColorMaterial(DefaultShaders, { 1.0f, 0.85f, 0.2f, 1.0f }, &Gfx);
        OwnedMaterials.push_back(matGoal);
        if (auto* srv = TexCache.Get(L"goal.png")) matGoal->SetTexture(srv);

        auto* matMission = new ColorMaterial(DefaultShaders, { 1, 1, 1, 1 }, &Gfx);
        OwnedMaterials.push_back(matMission);
        if (auto* srv = TexCache.Get(L"mission_complete.png")) matMission->SetTexture(srv);

        // 골 위치 및 판정 크기 - 좌클릭으로 좌표 확인 후 수정
        auto* goal = new GameObject(0.0f, 50.0f);
        goal->Scale = { 3.3f, 0.85f };

        goal->AddComponent(new MeshRenderer(QuadMesh, matGoal));

        auto* gc = new GoalComp();
        gc->Player = player;
        gc->Cleared = &Cleared;
        goal->AddComponent(gc);

        auto* banner = new MissionBanner();
        banner->Cam = &Cam;
        banner->Cleared = &Cleared;
        banner->Mat = matMission;
        banner->Quad = QuadMesh;
        goal->AddComponent(banner);

        World.push_back(goal);
    }

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

    // 패턴 플랫폼 교대 활성화 및 깜빡임 처리
    void UpdatePatternPlatforms(float dt)
    {
        if (PatternPlatforms.size() < 2) return;

        PatternTimer += dt;

        int current = PatternIndex;
        int next = (PatternIndex + 1) % PatternPlatforms.size();

        // 전환 직전 0.2초 동안 사라질 플랫폼만 깜빡임
        float blinkStart = PatternInterval - 0.2f;
        if (PatternTimer >= blinkStart)
        {
            bool visible = ((int)(PatternTimer * 10.0f) % 2) == 0;
            PatternPlatforms[current]->Owner->Visible = visible;
            PatternPlatforms[next]->Owner->Visible = true;
        }
        else
        {
            PatternPlatforms[current]->Owner->Visible = true;
            PatternPlatforms[next]->Owner->Visible = true;
        }

        if (PatternTimer >= PatternInterval)
        {
            PatternTimer = 0.0f;
            PatternIndex++;
            if (PatternIndex >= (int)PatternPlatforms.size()) PatternIndex = 0;

            // 전부 비활성화 후 현재 + 다음 두 개만 활성화
            for (auto* plat : PatternPlatforms)
            {
                plat->IsActive = false;
                plat->Owner->Visible = false;
            }

            current = PatternIndex;
            next = (PatternIndex + 1) % PatternPlatforms.size();

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

        // 렌더 순서: 배경/플랫폼 -> 아이템 -> 깃발 -> 플레이어/UI
        for (auto* go : Platforms)   go->Render(&Gfx);
        for (auto* go : RandomBoxes) go->Render(&Gfx);
        if (FlagObject) FlagObject->Render(&Gfx);
        for (auto* go : World)       go->Render(&Gfx);

        Gfx.SwapChain->Present(1, 0);
    }
};

// WndProc에서 GameLoop에 접근하기 위한 전역 포인터
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