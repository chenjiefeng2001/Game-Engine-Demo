-- ═══════════════════════════════════════════════════
-- sandbox_player.lua — Scripting v1 产品验收脚本
--
-- 演示契约：
--   Engine.input.is_down(key)   键盘状态（GLFW 后端）
--   _PERSIST.h                  实体句柄跨热重载保留（F5）
--   Engine.transform.translate  驱动真实 GameObject 位移 → Renderer 可见
--
-- 操作：WASD 移动绿色方块（Cube 为静态蓝色参照物）
-- ═══════════════════════════════════════════════════

_PERSIST = _PERSIST or {}
_PERSIST.h = _PERSIST.h or Engine.entity.spawn('Player')

local SPEED = 6.0

function OnCreate()
    Engine.log.info('player.lua OnCreate: handle=' .. tostring(_PERSIST.h))
    Engine.transform.set_position(_PERSIST.h, 2.0, 0.0, 0.0)
end

function OnUpdate(dt)
    local dx, dz = 0, 0
    if Engine.input.is_down('W') then dz = dz - 1 end
    if Engine.input.is_down('S') then dz = dz + 1 end
    if Engine.input.is_down('A') then dx = dx - 1 end
    if Engine.input.is_down('D') then dx = dx + 1 end

    if dx ~= 0 or dz ~= 0 then
        Engine.transform.translate(_PERSIST.h, dx * SPEED * dt, 0, dz * SPEED * dt)
    end
end
