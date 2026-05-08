# game_project
# Jump Game

DirectX 11 기반 2D 플랫포머 게임입니다.  
점프 충전 시스템과 다양한 지형 타입을 특징으로 하며, 단일 C++ 파일로 구성되어 있습니다.

---

### 링크 라이브러리

```
d3d11.lib / dxgi.lib / d3dcompiler.lib / windowscodecs.lib
```

---

## 빌드 방법

Visual Studio 에서 해당 리포지토리 복제
https://github.com/choi4599/game_project

---

## 조작키

| 키 | 동작 |
|----|------|
| `←` `→` | 좌우 이동 |
| `Space` 홀드 | 점프 충전 (홀드 중 이동 불가) |
| `Space` 릴리즈 | 점프 발사 (충전량에 비례한 높이) |
| `ESC` | 게임 종료 |

---

## 개발자 도구(맵 제작 및 테스트 용도)

| 키 / 동작 | 기능 |
|-----------|------|
| `F` | 플라이 모드 토글 — 중력 무시, 4방향 자유 이동 (`↑↓←→`) |
| `R` | 마지막 체크포인트로 리셋 |
| `C` | 현재 위치를 체크포인트로 저장 |
| 마우스 좌클릭 | 클릭 위치의 월드 좌표를 콘솔에 출력 |

실행 시 콘솔 창이 함께 열리며, 텍스처 로드 결과 및 각종 상태가 출력됩니다.

---

## 플랫폼 타입

| 타입 | 색상(텍스처 없을 때) | 동작 |
|------|----------------------|------|
| `Normal` | 갈색 | 기본 지면, 가속/감속 이동 |
| `Ice` | 하늘색 | 미끄러짐, 느린 가속/감속 |
| `PassThrough` | 초록 | 위에서만 착지, 아래서 통과 가능 |

---

## 텍스처 적용 방법

실행 파일과 **같은 폴더**에 PNG 파일을 배치합니다.
*현재 넣어둔 png파일은 테스트용으로 넣어둔 것이니 자유롭게 수정하셔도 됩니다.

```
game.exe
player.png
ground1.png
ground2.png
ice1.png
```

`main.cpp` 의 `BuildMap()` 에서 플랫폼 배치 시 마지막 인자로 파일명을 지정합니다.

```cpp
// 텍스처 없음 → 단색
AddPlatform(PlatformType::Normal, -5.0f, -1.0f, 5.0f, 0.0f);

// 텍스처 적용
AddPlatform(PlatformType::Normal, -3.0f, 1.5f, 0.0f, 1.8f, L"ground1.png");

// 같은 타입에 다른 텍스처
AddPlatform(PlatformType::Normal, -1.0f, 3.5f, 2.0f, 3.8f, L"ground2.png");
```

텍스처 파일이 없으면 크래시 없이 단색으로 자동 대체됩니다.

## 맵 제작

`BuildMap()` 함수 안에서 플랫폼을 추가합니다.

```cpp
// AddPlatform(타입, 왼쪽X, 아래Y, 오른쪽X, 위Y)
// AddPlatform(타입, 왼쪽X, 아래Y, 오른쪽X, 위Y, L"텍스처.png")

AddPlatform(PlatformType::Normal,      -5.0f, -1.0f,  5.0f, 0.0f);
AddPlatform(PlatformType::Ice,          1.0f,  1.5f,  3.5f, 1.8f, L"ice1.png");
AddPlatform(PlatformType::PassThrough, -2.0f,  7.0f,  0.5f, 7.2f);
```

좌표를 모를 때는 플라이 모드(`F`)로 이동 후 마우스 좌클릭으로 월드 좌표를 확인합니다.

*현재 배치해둔 플렛폼은 테스트용으로 배치해둔 것이니 자유롭게 수정하셔도 됩니다.

---

## 물리 상수 조정

`main.cpp` 상단의 상수를 수정해 게임 느낌을 조절합니다.

```cpp
GRAVITY       = -18.0f   // 중력 (음수, 클수록 빠르게 떨어짐)
JUMP_MAX      =   9.0f   // 최대 점프 속도 (완전 충전 시)
JUMP_MIN      =   3.0f   // 최소 점프 속도 (충전 없이 릴리즈)
JUMP_CHARGE   =   1.5f   // 초당 충전량 (클수록 빨리 충전)
MOVE_SPEED    =   4.0f   // 최대 이동 속도
GROUND_ACCEL  =  20.0f   // 지면 가속도 (클수록 즉각 반응)
GROUND_DECEL  =  20.0f   // 지면 감속도 (클수록 빨리 멈춤)
ICE_ACCEL     =   3.0f   // 얼음 가속도
ICE_DRAG      =   0.5f   // 얼음 감속도
AIR_ACCEL     =   2.0f   // 공중 보정 가속도
```

---

## 코드 확장 가이드

### 새 플랫폼 타입 추가

```cpp
// 1. PlatformType 열거형에 추가
enum class PlatformType { Normal, Ice, PassThrough, Lava };

// 2. AddPlatform() 의 switch 에 기본 색상 추가
case PlatformType::Lava: color = {1.0f, 0.3f, 0.0f, 1.0f}; break;

// 3. UpdateMovement() 의 2단계 분기에 이동 동작 추가
else if (OnLava) { /* 이동 동작 */ }

// ※ 점프 충전 중 이동 차단(1단계)은 자동으로 적용됨
```

### 새 컴포넌트 추가

```cpp
class MyComponent : public Component
{
public:
    void Start (GraphicsContext* gfx) override { /* 초기화 */ }
    void Input () override               { /* 입력 처리 */ }
    void Update(float dt) override       { /* 매 프레임 */ }
    void Render(GraphicsContext* gfx) override { /* 렌더링 */ }
};

// 원하는 GameObject 에 부착
gameObject->AddComponent(new MyComponent());
```

---

## 구조 개요

```
GameLoop
├── WindowContext       윈도우 생성 및 관리
├── GraphicsContext     DirectX 11 디바이스, 스왑체인, 블렌드
├── Camera              플레이어 추적, 월드↔스크린 좌표 변환
├── TextureCache        PNG 로드 및 중복 방지
├── World[]             플레이어 등 일반 오브젝트
│   └── GameObject
│       ├── MeshRenderer
│       ├── PlayerController
│       └── JumpChargeBar
└── Platforms[]         충돌 대상 플랫폼
    └── GameObject
        ├── MeshRenderer
        └── PlatformComp    타입 + AABB (Pos/Scale 기반)
```
