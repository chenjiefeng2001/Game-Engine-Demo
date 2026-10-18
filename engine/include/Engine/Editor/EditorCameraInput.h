#pragma once

namespace Engine {

    /// Phase 7: External input state for EditorCamera (Avalonia → Bridge → Camera).
    /// Pure POD struct with no ImGui dependency.
    struct EditorCameraInput {
        float mouseDeltaX = 0.0f;
        float mouseDeltaY = 0.0f;
        float mouseWheel = 0.0f;
        bool  rightDown = false;
        bool  middleDown = false;
        bool  leftDown = false;
        bool  altHeld = false;
        bool  shiftHeld = false;
        bool  keyW = false, keyS = false, keyA = false, keyD = false;
        bool  keyE = false, keyQ = false;
        bool  keyUp = false, keyDown = false, keyLeft = false, keyRight = false;
        float deltaTime = 0.016f;
    };

} // namespace Engine
