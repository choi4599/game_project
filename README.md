# 슬라임 점프

DirectX 11로 만든 2D 플랫포머 게임입니다.
점프킹처럼 점프를 충전해서 발사하는 방식이며, 다양한 지형과 아이템 룰렛이 있습니다.
모든 코드는 `main.cpp` 단일 파일에 작성되어 있습니다.

notion : https://app.notion.com/p/360efd16055280acb23dd35dab49b2f5

---

## 실행 환경

- Windows 10 이상
- Visual Studio 2019 이상
- DirectX 11 지원 GPU

---

## 빌드 방법

```
git clone https://github.com/choi4599/game_project.git
```

Visual Studio에서 프로젝트를 열고, 아래 라이브러리가 링크되어 있는지 확인합니다.

```
d3d11.lib
dxgi.lib
d3dcompiler.lib
windowscodecs.lib
```

빌드 후 실행 파일 옆에 텍스처 PNG 파일들을 배치하면 됩니다.
PNG가 없어도 크래시 없이 단색으로 대체됩니다.

---

## 조작키

| 키 | 동작 |
|---|---|
| `← →` | 좌우 이동 |
| `Space` 홀드 | 점프 충전 (홀드 중에는 이동 불가) |
| `Space` 릴리즈 | 점프 발사 (오래 누를수록 높이 점프) |
| `R` | 체크포인트 아이템이 사용된 지점으로 되돌아가기 |
| `ESC` | 게임 종료 |

### 테스트용 키

맵을 만들거나 디버그할 때 쓰는 키입니다.

| 키 | 기능 |
|---|---|
| `F` | 플라이 모드 — 중력 무시하고 `↑↓←→`로 자유 이동 (플렛폼 통과 가능) |
| `1` | Fly 아이템 즉시 발동 (플렛폼 통과 불가) |
| `2` | PassThrough 아이템 즉시 발동 |
| `3` | Shield 아이템 즉시 발동 |
| `4` | Checkpoint 아이템 즉시 발동 |
| 마우스 좌클릭 | 클릭한 위치의 월드 좌표를 콘솔에 출력 |

실행하면 콘솔 창이 함께 열립니다. 텍스처 로드 여부, 아이템 발동, 좌표 등이 여기에 출력됩니다.

---

## 게임 구성

### 플랫폼 종류

| 이미지 | 타입 | 동작 |
|---|---|---|
| <img width="100" height="50" alt="ground1" src="https://github.com/user-attachments/assets/11d8d878-a06d-4ff3-b473-7f8c5cb20bf2" /> | Normal | 기본 지면 |
| <img width="100" height="50" alt="ice1" src="https://github.com/user-attachments/assets/4853e289-de4a-4695-93fe-bf0c57f7b62e" /> | Ice | 미끄러워서 멈추기 어렵고 방향 전환이 느림 |
| <img width="100" height="50" alt="vanishing1" src="https://github.com/user-attachments/assets/f1069d2b-2041-43ad-b93b-6cfd5e58b4ba" /> | Vanishing | 밟으면 3초 후 사라지고 5초 후 다시 나타남 |
| <img width="100" height="50" alt="reverse1" src="https://github.com/user-attachments/assets/d34af826-a372-4315-ae5b-62871dcf0390" /> | Reverse | 밟으면 다른 플렛폼 착지 전까지 2초간 좌우 조작이 반전됨 |
| <img width="100" height="50" alt="moving1" src="https://github.com/user-attachments/assets/6840de78-6829-4092-91f5-5963adafaa74" /> | Moving | 좌우로 자동으로 왕복 이동 |
| <img width="100" height="50" alt="stonewall" src="https://github.com/user-attachments/assets/25aaffbc-a163-4279-96bf-c270f40ba6b3" /> | Wall | 양쪽 경계 벽, 플레이어가 닿으면 튕겨남 |
| 그라데이션 이미지 | Background | 배경 이미지 전용, 충돌 판정 없음 |
| 흔들리는 플렛폼 | Bomb | 일정 시간마다 흔들리면서 플레이어를 튕겨냄 |

### 아이템

맵 곳곳에 랜덤박스가 놓여 있습니다. 닿으면 룰렛이 돌아가고 멈춘 칸의 아이템이 발동됩니다.
발동 중인 아이템은 플레이어 머리 위에 남은 시간 바로 표시됩니다.
랜덤박스는 30초 후 다시 나타납니다.

| 이미지 | 아이템 | 효과 | 지속 시간 |
|---|---|---|---|
| <img width="40" height="40" alt="item_fly" src="https://github.com/user-attachments/assets/22238f73-b594-455f-9a30-ffe831f534cf" /> | Fly | 중력 무시하고 4방향으로 천천히 비행 (플렛폼 통과 불가) | 3초 |
| <img width="40" height="40" alt="item_passthrough" src="https://github.com/user-attachments/assets/7fe3b4fe-4e22-4835-b901-0d8ec4cf2bbf" /> | PassThrough | 모든 플랫폼(벽 제외)의 좌우/하단 통과 가능 | 5초 |
| <img width="40" height="40" alt="item_shield" src="https://github.com/user-attachments/assets/1c43a3b2-410d-43d2-b0c3-47d9272cee66" /> | Shield | 특수 플렛폼의 효과를 받지 않음 | 8초 |
| <img width="40" height="40" alt="item_checkpoint" src="https://github.com/user-attachments/assets/648175ec-4c0f-4d25-899e-8aa584bba771" /> | Checkpoint | 현재 위치를 체크포인트로 저장 (`R`키로 복귀) | 즉시 |

*각 아이템 효과가 적용 중일 때 해당하는 플레이어 이미지를 출력합니다(여러 효과 적용 시 우선순위: fly>passthrough>shield)

---

## 텍스처 파일 목록

실행 파일과 같은 폴더에 아래 PNG 파일을 넣으면 적용됩니다.
없으면 단색으로 자동 대체됩니다.

| 파일명 | 용도 |
|---|---|
| `player.png` | 기본 플레이어 |
| `player_fly.png` | Fly 아이템 발동 중 플레이어 |
| `player_passthrough.png` | PassThrough 아이템 발동 중 플레이어 |
| `player_shield.png` | Shield 아이템 발동 중 플레이어 |
| `ground1.png` / `ground2.png` | 일반 지면 |
| `ice1.png` | 얼음 지면 |
| `vanishing1.png` / `vanishing2.png` | 사라지는 발판 |
| `reverse1.png` | 조작 반전 발판 |
| `moving1.png` / `moving2.png` | 이동 발판 |
| `background1__.png` / `background2_.png` / `background3_.png` | 배경 |
| `stoneWall.png` | 양쪽 벽 |
| `item_randombox.png` | 랜덤박스 |
| `item_fly.png` / `item_passthrough.png` / `item_shield.png` / `item_checkpoint.png` | 룰렛 아이콘 |
| `flag_mast.png` / `flag_flag.png` | 체크포인트 깃발 |
| `goal.png` | 골 지점 마커 |
| `mission_complete.png` | 클리어 배너 |

> 현재 포함된 PNG 파일은 테스트용이므로 자유롭게 교체하셔도 됩니다.

---

## 맵 수정 방법

`main.cpp`의 `BuildMap()` 함수에서 플랫폼 위치를 추가하거나 바꿀 수 있습니다.

```cpp
// AddPlatform(타입, 왼쪽X, 아래Y, 오른쪽X, 위Y)
// AddPlatform(타입, 왼쪽X, 아래Y, 오른쪽X, 위Y, L"텍스처.png")

AddPlatform(PlatformType::Normal, -3.0f, 1.5f, 0.0f, 1.8f, L"ground1.png");
AddPlatform(PlatformType::Ice,     1.0f, 1.5f, 3.5f, 1.8f, L"ice1.png");
```

좌표를 모를 때는 플라이 모드(`F`)로 원하는 위치까지 이동한 뒤 마우스 좌클릭을 하면
콘솔에 월드 좌표가 출력됩니다. 그 값을 그대로 넣으면 됩니다.

랜덤박스는 `BuildRandomBoxes()` 함수에서 추가합니다.

```cpp
AddRandomBox(cx, cy);
AddRandomBox(cx, cy, L"tex.png");
```

골 지점은 `BuildGoal()` 함수 안의 좌표를 바꾸면 됩니다.

```cpp
auto* goal = new GameObject(0.0f, 50.0f);  // 이 좌표가 골 위치
goal->Scale = { 3.3f, 0.85f };             // 닿을 판정 영역 크기
```

> 현재 배치된 플랫폼은 테스트용이므로 자유롭게 수정하셔도 됩니다.

---

## 물리 수치 조정

`main.cpp` 상단에 게임 느낌을 결정하는 상수들이 모여 있습니다.

```cpp
GRAVITY      = -18.0f  // 중력. 절댓값이 클수록 빠르게 떨어짐
JUMP_MAX     =   9.0f  // 스페이스를 꽉 눌렀을 때 점프 속도
JUMP_MIN     =   3.0f  // 스페이스를 바로 뗐을 때 점프 속도
JUMP_CHARGE  =   1.5f  // 충전 속도. 클수록 빨리 꽉 참
MOVE_SPEED   =   2.0f  // 최대 이동 속도
GROUND_ACCEL =  10.0f  // 지면 가속도. 클수록 즉각 반응
GROUND_DECEL =  20.0f  // 지면 감속도. 클수록 빨리 멈춤
ICE_ACCEL    =   3.0f  // 얼음 위 가속도
ICE_DRAG     =   0.5f  // 얼음 위 감속도
```

---

## 코드 구조

```
GameLoop                         전체 게임 루프 관리
├── WindowContext                윈도우 생성
├── GraphicsContext              DirectX 11 디바이스, 스왑체인
├── Camera                       X축 고정, Y축 화면 단위 컷 전환
├── TextureCache                 PNG 로드 및 중복 방지
│
├── World[]                      일반 오브젝트 목록
│   └── GameObject
│       ├── MeshRenderer         렌더링
│       ├── PlayerController     물리, 충돌, 입력, 아이템, 룰렛
│       ├── JumpChargeBar        점프 충전량 바 표시
│       ├── ActiveItemBar        아이템 잔여시간 바 표시
│       └── RouletteUI           룰렛 아이콘 표시
│
├── Platforms[]                  충돌 대상 플랫폼 목록
│   └── GameObject
│       ├── MeshRenderer
│       └── PlatformComp         타입별 동작 (이동, 소멸 등)
│
├── RandomBoxes[]                랜덤박스 목록
│   └── GameObject
│       ├── MeshRenderer
│       └── RandomBoxComp        접촉 감지 → 룰렛 시작
│
└── FlagObject                   체크포인트 깃발
    └── CheckpointFlagRenderer   깃대 + 깃발 렌더링
```

모든 오브젝트는 `Component`를 상속한 컴포넌트를 `GameObject`에 붙이는 구조입니다.
`GameLoop`의 `Update()`와 `Render()`에서 매 프레임마다 전체 오브젝트를 순회합니다.
