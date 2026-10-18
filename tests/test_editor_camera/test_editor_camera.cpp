// Phase 7-D — EditorCamera external input path
//
// 目标：给 orbit/pan/fly 的 **External 路径**提供可执行证据。
// 在此之前 `tests/` 下对 EditorCamera / orbit / external input 的搜索命中数为 0，
// 也就是说 Avalonia 转发过来的输入在引擎侧的行为完全没有断言保护。
//
// 本 target 的关键性质：**全程不创建 ImGui context**。
// 这不是风格选择，而是回归测试的前提 —— 修复前 `EditorCamera::ProcessInput`
// 的 Alt+左键 orbit 分支硬编码 `ImGui::IsMouseDown(ImGuiMouseButton_Left)`，
// 完全绕开了 External 分支已经算好的 `leftDown` 局部变量。生产路径
// （EngineHost headless）没有 ImGui context，那一行要么崩、要么读到
// ImGui 自己的鼠标状态而不是 Avalonia 转发来的值。
// 因此每个用例都必须能在无 context 下跑完：若有人把 ImGui 依赖改回来，
// 本 target 会立刻失败，而不是等到 Avalonia 里点右键才发现。
//
// 断言一律按 ProcessInput 里的公式反推期望值（见 EditorCamera.cpp:106-163），
// 不用"函数返回非零"这类弱断言。

#include <gtest/gtest.h>

#include <cmath>

#include "Engine/Editor/EditorCamera.h"
#include "Engine/Editor/EditorCameraInput.h"

namespace {

using Engine::EditorCamera;
using Engine::EditorCameraInput;
using Engine::Vec3;

constexpr float kDt = 0.1f;
constexpr float kTol = 1e-4f;

// Reset() 后的初值（EditorCamera.cpp:20-30）
constexpr float kInitYaw = 45.0f;
constexpr float kInitPitch = -30.0f;
constexpr float kInitDistance = 10.0f;

float Length(const Vec3& v) {
    return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}

Vec3 Sub(const Vec3& a, const Vec3& b) {
    return Vec3{a.x - b.x, a.y - b.y, a.z - b.z};
}

/// 把相机切到「已激活 + External 输入模式」。
/// 用引用出参而不是按值返回：EditorCamera 删除了拷贝构造且没有移动构造，
/// 按值返回只能靠 NRLO，而 NRLO 是可选的，不能依赖。
void MakeExternalCamera(EditorCamera& cam) {
    cam.SetExternalInputMode(true);
    cam.SetActive(true);
}

EditorCameraInput Input() {
    return EditorCameraInput{};
}

// ────────────────────────────────────────────────────────────────────
// Orbit：Alt + 左键
// ────────────────────────────────────────────────────────────────────

// 回归用例：修复前这一行读的是 ImGui 的鼠标状态，External 模式读不到。
TEST(EditorCameraExternal, Orbit_AltLeftRotatesYawAndPitch) {
    EditorCamera cam;    MakeExternalCamera(cam);

    EditorCameraInput in = Input();
    in.altHeld = true;
    in.leftDown = true;
    in.mouseDeltaX = 10.0f;
    in.mouseDeltaY = 5.0f;
    cam.SetExternalInput(in);

    cam.ProcessInput(kDt);

    // EditorCamera.cpp:158-160 —— yaw += dx*0.3，pitch -= dy*0.3
    EXPECT_NEAR(cam.GetYaw(), kInitYaw + 3.0f, kTol);
    EXPECT_NEAR(cam.GetPitch(), kInitPitch - 1.5f, kTol);
}

// orbit 需要 Alt 与左键**同时**按下，缺一不转。
TEST(EditorCameraExternal, Orbit_RequiresAltAndLeftTogether) {
    {
        EditorCamera cam;        MakeExternalCamera(cam);
        EditorCameraInput in = Input();
        in.leftDown = true;          // 无 Alt
        in.mouseDeltaX = 10.0f;
        in.mouseDeltaY = 5.0f;
        cam.SetExternalInput(in);
        cam.ProcessInput(kDt);
        EXPECT_NEAR(cam.GetYaw(), kInitYaw, kTol);
        EXPECT_NEAR(cam.GetPitch(), kInitPitch, kTol);
    }
    {
        EditorCamera cam;        MakeExternalCamera(cam);
        EditorCameraInput in = Input();
        in.altHeld = true;           // 无左键
        in.mouseDeltaX = 10.0f;
        in.mouseDeltaY = 5.0f;
        cam.SetExternalInput(in);
        cam.ProcessInput(kDt);
        EXPECT_NEAR(cam.GetYaw(), kInitYaw, kTol);
        EXPECT_NEAR(cam.GetPitch(), kInitPitch, kTol);
    }
}

// Alt+左键在 EditorCamera.cpp:124 同时命中 Pan 分支（middleDown || (altHeld && leftDown)），
// 因此该组合**既 pan 又 orbit**。这是既有行为（legacy 与 external 同样如此），
// 此处显式钉住，避免后续误以为是回归。
TEST(EditorCameraExternal, AltLeft_TriggersBothPanAndOrbit) {
    EditorCamera cam;    MakeExternalCamera(cam);

    EditorCameraInput in = Input();
    in.altHeld = true;
    in.leftDown = true;
    in.mouseDeltaX = 10.0f;
    in.mouseDeltaY = 5.0f;
    cam.SetExternalInput(in);

    const Vec3 before = cam.GetFocusPoint();
    cam.ProcessInput(kDt);

    EXPECT_NEAR(cam.GetYaw(), kInitYaw + 3.0f, kTol);   // orbit 生效
    EXPECT_GT(Length(Sub(cam.GetFocusPoint(), before)), kTol);  // pan 同时生效
}

// ────────────────────────────────────────────────────────────────────
// Pan：中键拖拽
// ────────────────────────────────────────────────────────────────────

// pan 把焦点沿相机平面平移：-right*dx*speed + up*dy*speed，
// speed = distance*0.0015（EditorCamera.cpp:124-129）。
TEST(EditorCameraExternal, Pan_MiddleDragMovesFocusInCameraPlane) {
    EditorCamera cam;    MakeExternalCamera(cam);

    // pan 不改变 yaw/pitch（无 Alt 时 orbit 不触发），所以方向基可预先取用
    const Vec3 right = cam.GetRightVector();
    const Vec3 up = cam.GetUpVector();

    const float dx = 12.0f, dy = -7.0f;
    const float panSpeed = cam.GetDistance() * 0.0015f;

    EditorCameraInput in = Input();
    in.middleDown = true;
    in.mouseDeltaX = dx;
    in.mouseDeltaY = dy;
    cam.SetExternalInput(in);

    const Vec3 before = cam.GetFocusPoint();
    cam.ProcessInput(kDt);
    const Vec3 moved = Sub(cam.GetFocusPoint(), before);

    EXPECT_NEAR(moved.x, (-right.x * dx + up.x * dy) * panSpeed, kTol);
    EXPECT_NEAR(moved.y, (-right.y * dx + up.y * dy) * panSpeed, kTol);
    EXPECT_NEAR(moved.z, (-right.z * dx + up.z * dy) * panSpeed, kTol);

    // 焦点不应沿前方向移动（那是 fly 的语义）
    EXPECT_NEAR(moved.x * cam.GetForwardVector().x +
                moved.y * cam.GetForwardVector().y +
                moved.z * cam.GetForwardVector().z,
                0.0f, kTol);
}

// ────────────────────────────────────────────────────────────────────
// Fly：右键 + WASD/QE
// ────────────────────────────────────────────────────────────────────

TEST(EditorCameraExternal, Fly_RightDragRotatesYawAndPitch) {
    EditorCamera cam;    MakeExternalCamera(cam);

    EditorCameraInput in = Input();
    in.rightDown = true;
    in.mouseDeltaX = 10.0f;
    in.mouseDeltaY = 5.0f;
    cam.SetExternalInput(in);

    cam.ProcessInput(kDt);

    // EditorCamera.cpp:134-135 —— fly 用 0.2 灵敏度，orbit 用 0.3
    EXPECT_NEAR(cam.GetYaw(), kInitYaw + 2.0f, kTol);
    EXPECT_NEAR(cam.GetPitch(), kInitPitch - 1.0f, kTol);
}

// W 前进：位移量 = flySpeed*dt，沿前方向；此处 delta 为 0 以隔离平移与旋转。
TEST(EditorCameraExternal, Fly_KeyWTranslatesAlongForward) {
    EditorCamera cam;    MakeExternalCamera(cam);
    const Vec3 forward = cam.GetForwardVector();

    EditorCameraInput in = Input();
    in.rightDown = true;
    in.keyW = true;
    cam.SetExternalInput(in);

    const Vec3 before = cam.GetFocusPoint();
    cam.ProcessInput(kDt);
    const Vec3 moved = Sub(cam.GetFocusPoint(), before);

    const float expect = cam.GetFlySpeed() * kDt;
    EXPECT_NEAR(Length(moved), expect, kTol);
    EXPECT_NEAR(moved.x, forward.x * expect, kTol);
    EXPECT_NEAR(moved.y, forward.y * expect, kTol);
    EXPECT_NEAR(moved.z, forward.z * expect, kTol);
}

// E 上升：沿世界 Y 轴，不随相机朝向改变。
TEST(EditorCameraExternal, Fly_KeyETranslatesAlongWorldUp) {
    EditorCamera cam;    MakeExternalCamera(cam);

    EditorCameraInput in = Input();
    in.rightDown = true;
    in.keyE = true;
    cam.SetExternalInput(in);

    const Vec3 before = cam.GetFocusPoint();
    cam.ProcessInput(kDt);
    const Vec3 moved = Sub(cam.GetFocusPoint(), before);

    const float expect = cam.GetFlySpeed() * kDt;
    EXPECT_NEAR(moved.x, 0.0f, kTol);
    EXPECT_NEAR(moved.y, expect, kTol);
    EXPECT_NEAR(moved.z, 0.0f, kTol);
}

// Shift 三倍速（EditorCamera.cpp:149）。
TEST(EditorCameraExternal, Fly_ShiftHeldTriplesSpeed) {
    EditorCamera normal;    MakeExternalCamera(normal);
    EditorCamera boosted;    MakeExternalCamera(boosted);

    EditorCameraInput base = Input();
    base.rightDown = true;
    base.keyW = true;

    normal.SetExternalInput(base);
    normal.ProcessInput(kDt);
    const float plain = Length(Sub(normal.GetFocusPoint(), Vec3{0, 0, 0}));

    EditorCameraInput fast = base;
    fast.shiftHeld = true;
    boosted.SetExternalInput(fast);
    boosted.ProcessInput(kDt);
    const float shifted = Length(Sub(boosted.GetFocusPoint(), Vec3{0, 0, 0}));

    EXPECT_NEAR(plain, normal.GetFlySpeed() * kDt, kTol);
    EXPECT_NEAR(shifted, plain * 3.0f, kTol);
}

// 俯仰限位：±89 度。
TEST(EditorCameraExternal, Fly_PitchIsClampedTo89Degrees) {
    EditorCamera cam;    MakeExternalCamera(cam);

    EditorCameraInput in = Input();
    in.rightDown = true;
    in.mouseDeltaY = -1000.0f;   // 向上猛拉
    cam.SetExternalInput(in);
    cam.ProcessInput(kDt);
    EXPECT_NEAR(cam.GetPitch(), 89.0f, kTol);

    EditorCamera cam2;
    MakeExternalCamera(cam2);
    EditorCameraInput in2 = Input();
    in2.rightDown = true;
    in2.mouseDeltaY = 1000.0f;   // 向下猛压
    cam2.SetExternalInput(in2);
    cam2.ProcessInput(kDt);
    EXPECT_NEAR(cam2.GetPitch(), -89.0f, kTol);
}

// 右键 + Alt 时 fly 让位给 pan/orbit，不应移动焦点。
TEST(EditorCameraExternal, Fly_IsSuppressedByAlt) {
    EditorCamera cam;    MakeExternalCamera(cam);

    EditorCameraInput in = Input();
    in.rightDown = true;
    in.altHeld = true;
    in.keyW = true;
    cam.SetExternalInput(in);

    const Vec3 before = cam.GetFocusPoint();
    cam.ProcessInput(kDt);

    EXPECT_NEAR(Length(Sub(cam.GetFocusPoint(), before)), 0.0f, kTol);
    EXPECT_NEAR(cam.GetYaw(), kInitYaw, kTol);
}

// ────────────────────────────────────────────────────────────────────
// 滚轮缩放
// ────────────────────────────────────────────────────────────────────

// 滚轮改的是 m_SmoothZoom，距离由 OnUpdate 指数逼近（EditorCamera.cpp:117-121, 170-173）。
TEST(EditorCameraExternal, Scroll_WheelUpZoomsInAndDampingConverges) {
    EditorCamera cam;    MakeExternalCamera(cam);

    EditorCameraInput in = Input();
    in.mouseWheel = 1.0f;        // smoothZoom = 10 - 10*0.2 = 8
    cam.SetExternalInput(in);
    cam.ProcessInput(kDt);

    cam.OnUpdate(1.0f);          // exp(-15) 收敛到 ~0.9999997
    EXPECT_NEAR(cam.GetDistance(), 8.0f, 1e-2f);
    EXPECT_LT(cam.GetDistance(), kInitDistance);
}

TEST(EditorCameraExternal, Scroll_WheelDownZoomsOut) {
    EditorCamera cam;    MakeExternalCamera(cam);

    EditorCameraInput in = Input();
    in.mouseWheel = -1.0f;       // smoothZoom = 10 + 10*0.2 = 12
    cam.SetExternalInput(in);
    cam.ProcessInput(kDt);

    cam.OnUpdate(1.0f);
    EXPECT_NEAR(cam.GetDistance(), 12.0f, 1e-2f);
    EXPECT_GT(cam.GetDistance(), kInitDistance);
}

// ────────────────────────────────────────────────────────────────────
// 开关语义
// ────────────────────────────────────────────────────────────────────

// m_Active 为假时整段输入被忽略（EditorCamera.cpp:60）。
TEST(EditorCameraExternal, InactiveCamera_IgnoresAllInput) {
    EditorCamera cam;    MakeExternalCamera(cam);
    cam.SetActive(false);

    EditorCameraInput in = Input();
    in.rightDown = true;
    in.leftDown = true;
    in.altHeld = true;
    in.middleDown = true;
    in.keyW = true;
    in.mouseWheel = 1.0f;
    in.mouseDeltaX = 100.0f;
    in.mouseDeltaY = 100.0f;
    cam.SetExternalInput(in);

    const Vec3 before = cam.GetFocusPoint();
    cam.ProcessInput(kDt);
    cam.OnUpdate(kDt);

    EXPECT_NEAR(cam.GetYaw(), kInitYaw, kTol);
    EXPECT_NEAR(cam.GetPitch(), kInitPitch, kTol);
    EXPECT_NEAR(cam.GetDistance(), kInitDistance, kTol);
    EXPECT_NEAR(Length(Sub(cam.GetFocusPoint(), before)), 0.0f, kTol);
}

TEST(EditorCameraExternal, ExternalInputMode_TogglesIndependently) {
    EditorCamera cam;
    EXPECT_FALSE(cam.IsExternalInputMode());
    cam.SetExternalInputMode(true);
    EXPECT_TRUE(cam.IsExternalInputMode());
}

}  // namespace