-- ═══════════════════════════════════════════════════
-- dogfood_game.lua — Dogfood-01 v2（M005 + M001）
--
-- v2 变更：
--   M001: Engine.entity.find("Player") 取代硬编码句柄 1/5
--   M005: Engine.ui.text("Victory!") 取代纯 log 输出
-- ═══════════════════════════════════════════════════

_PERSIST = _PERSIST or {}

-- ── M001：按名称查找实体（不再依赖句柄序号）──
local player = Engine.entity.find("Player")
local goal   = Engine.entity.find("Goal")

local obstacles = {}
for _, name in ipairs({"Obstacle1", "Obstacle2", "Obstacle3"}) do
    local h = Engine.entity.find(name)
    if h then obstacles[#obstacles + 1] = h end
end

_PERSIST.playerH = player or 0
_PERSIST.goalH   = goal or 0
_PERSIST.obstacles = obstacles

local PL_R, OB_R, GOAL_R = 0.45, 0.75, 0.85
local SPOTS = {
    { -5.0, -5.0 }, { 5.0, -5.0 },
    {  0.0, -7.0 }, { 5.0,  3.0 },
}

local function dist2(ax, ay, bx, by)
    local dx, dz = ax - bx, ay - by
    return dx * dx + dz * dz
end

function OnCreate()
    if not _PERSIST.wins then
        Engine.ui.text("Reach the goal! (WASD)")
    end
end

function OnUpdate(dt)
    if not _PERSIST.playerH or _PERSIST.playerH == 0 then return end
    local x, y, z = Engine.transform.get_position(_PERSIST.playerH)
    if not x then return end

    -- ── 输入 → 意图位移 ──
    local mx, mz = 0, 0
    if Engine.input.is_down('W') then mz = mz - 1 end
    if Engine.input.is_down('S') then mz = mz + 1 end
    if Engine.input.is_down('A') then mx = mx - 1 end
    if Engine.input.is_down('D') then mx = mx + 1 end

    local SPEED = 6.0
    x = x + mx * SPEED * dt
    z = z + mz * SPEED * dt

    -- ── 障碍推挤 ──
    for _, oh in ipairs(_PERSIST.obstacles) do
        local ox, oy, oz = Engine.transform.get_position(oh)
        if ox then
            local d2 = dist2(x, z, ox, oz)
            local minD = PL_R + OB_R
            if d2 < minD * minD and d2 > 1e-6 then
                local d = math.sqrt(d2)
                local nx, nz = (x - ox) / d, (z - oz) / d
                x = ox + nx * minD
                z = oz + nz * minD
            end
        end
    end

    Engine.transform.set_position(_PERSIST.playerH, x, y, z)

    -- ── 目标检测 → 胜利循环 ──
    if not _PERSIST.goalH or _PERSIST.goalH == 0 then return end
    local gx, gy, gz = Engine.transform.get_position(_PERSIST.goalH)
    if not gx then return end

    if dist2(x, z, gx, gz) < (PL_R + GOAL_R) * (PL_R + GOAL_R) then
        _PERSIST.wins = (_PERSIST.wins or 0) + 1
        Engine.ui.text("VICTORY #" .. tostring(_PERSIST.wins))
        Engine.log.info("[Dogfood] VICTORY #" .. tostring(_PERSIST.wins))

        local idx = math.floor(Engine.random(1, #SPOTS)) + 1
        if idx > #SPOTS then idx = #SPOTS end
        local s = SPOTS[idx]
        Engine.transform.set_position(_PERSIST.goalH, s[1], 0.0, s[2])
    end
end