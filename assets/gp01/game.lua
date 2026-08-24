-- ═══════════════════════════════════════════
-- GP01 game.lua — Main 场景导演（GP1-A 骨架）
--
-- 阶段范围（见 docs/GP-P1-Charter.md §7）：
--   玩家 WASD 移动 + 场地边界推出 + HUD。
--   敌人/战斗/胜负在 GP1-B/C 逐步加入。
-- 全部使用冻结 API：entity.find/transform/input/ui/log
--
-- 已知契约约束（GP01-Ledger GP-001）：
--   沙箱移除 dofile/loadfile/require → 玩法代码单文件组织。
-- ═══════════════════════════════════════════

_PERSIST = _PERSIST or {}

-- ── 实体绑定 ──
local player = Engine.entity.find("Player")
local walls = {}
for _, n in ipairs({"Wall_N", "Wall_S", "Wall_W", "Wall_E"}) do
    local h = Engine.entity.find(n)
    if h then walls[#walls + 1] = h end
end

-- ── 状态 ──
_PERSIST.state = _PERSIST.state or "playing"
_PERSIST.hp    = _PERSIST.hp or 10
_PERSIST.score = _PERSIST.score or 0

-- ── 常量 ──
local SPEED  = 4.0
local PL_R   = 0.40
local WALL_R = 0.60

local function dist(ax, az, bx, bz)
    local dx, dz = ax - bx, az - bz
    return math.sqrt(dx * dx + dz * dz)
end

function OnCreate()
    Engine.log.info("[GP01] bootstrap: player bound=" ..
                    tostring(player ~= nil) ..
                    " walls=" .. tostring(#walls))
end

function OnUpdate(dt)
    if _PERSIST.state ~= "playing" then return end
    if not player then return end

    local px, py, pz = Engine.transform.get_position(player)

    -- 输入移动（WASD）
    local mx, mz = 0, 0
    if Engine.input.is_down('W') then mz = mz - 1 end
    if Engine.input.is_down('S') then mz = mz + 1 end
    if Engine.input.is_down('A') then mx = mx - 1 end
    if Engine.input.is_down('D') then mx = mx + 1 end
    local len = math.sqrt(mx * mx + mz * mz)
    if len > 0.01 then
        px = px + (mx / len) * SPEED * dt
        pz = pz + (mz / len) * SPEED * dt
    end

    -- 边界推出
    for _, wh in ipairs(walls) do
        local wx, wy, wz = Engine.transform.get_position(wh)
        if wx then
            local d = dist(px, pz, wx, wz)
            if d < PL_R + WALL_R and d > 0.01 then
                px = wx + (px - wx) / d * (PL_R + WALL_R)
                pz = wz + (pz - wz) / d * (PL_R + WALL_R)
            end
        end
    end

    Engine.transform.set_position(player, px, py, pz)

    Engine.ui.text("HP:" .. tostring(_PERSIST.hp) ..
                   " SCORE:" .. tostring(_PERSIST.score))
end
