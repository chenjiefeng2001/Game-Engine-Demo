-- ═══════════════════════════════════════════════════
-- dogfood02_game.lua — Dogfood-02 Resource-Driven Game
--
-- 约束：仅使用冻结的 Scripting API v2.1
--   Engine.input.is_down / Engine.entity.find / Engine.transform.*
--   Engine.ui.text / Engine.log.info / _PERSIST
--
-- 玩法：收集 3 个 Pickup → 到达 GoalZone → VICTORY
-- 所有实体由场景 JSON 提供，脚本通过 find() 绑定。
-- ═══════════════════════════════════════════════════

_PERSIST = _PERSIST or {}

-- ── 实体绑定（M001 entity.find 消除句柄硬编码）──
local player   = Engine.entity.find("Player")
local goal     = Engine.entity.find("GoalZone")
local pickups  = {}
for _, name in ipairs({"Pickup1", "Pickup2", "Pickup3"}) do
    local h = Engine.entity.find(name)
    if h then pickups[#pickups + 1] = { handle = h, collected = false } end
end
local obstacles = {}
for _, name in ipairs({"Obstacle1", "Obstacle2", "Obstacle3"}) do
    local h = Engine.entity.find(name)
    if h then obstacles[#obstacles + 1] = h end
end

_PERSIST.score    = _PERSIST.score or 0
_PERSIST.victory  = _PERSIST.victory or false

local SPEED       = 6.0
local PICKUP_R    = 0.6
local PL_R        = 0.45
local OB_R        = 0.75
local GOAL_R      = 1.0

local function dist2(ax, az, bx, bz)
    local dx, dz = ax - bx, az - bz
    return dx * dx + dz * dz
end

function OnCreate()
    Engine.ui.text("Collect all pickups! Score: " .. tostring(_PERSIST.score) .. "/3")
end

function OnUpdate(dt)
    if not player then return end
    local px, py, pz = Engine.transform.get_position(player)

    -- ── 移动 ──
    local mx, mz = 0, 0
    if Engine.input.is_down('W') then mz = mz - 1 end
    if Engine.input.is_down('S') then mz = mz + 1 end
    if Engine.input.is_down('A') then mx = mx - 1 end
    if Engine.input.is_down('D') then mx = mx + 1 end

    local len = math.sqrt(mx * mx + mz * mz)
    if len > 0.01 then
        mx = mx / len; mz = mz / len
        px = px + mx * SPEED * dt
        pz = pz + mz * SPEED * dt
    end

    -- ── 障碍推挤 ──
    for _, oh in ipairs(obstacles) do
        local ox, oy, oz = Engine.transform.get_position(oh)
        if ox then
            local d2 = dist2(px, pz, ox, oz)
            local minD = PL_R + OB_R
            if d2 < minD * minD and d2 > 1e-6 then
                local d = math.sqrt(d2)
                px = ox + (px - ox) / d * minD
                pz = oz + (pz - oz) / d * minD
            end
        end
    end

    -- ── Pickup 收集 ──
    for _, pk in ipairs(pickups) do
        if not pk.collected then
            local sx, sy, sz = Engine.transform.get_position(pk.handle)
            if sx and dist2(px, pz, sx, sz) < PICKUP_R * PICKUP_R then
                pk.collected = true
                _PERSIST.score = (_PERSIST.score or 0) + 1
                Engine.log.info("[Dogfood02] Pickup! Score: " .. tostring(_PERSIST.score))
            end
        end
    end

    Engine.transform.set_position(player, px, py, pz)

    -- ── 胜利条件：全部收集 + 到达 GoalZone ──
    if not _PERSIST.victory and _PERSIST.score >= #pickups then
        if goal then
            local gx, gy, gz = Engine.transform.get_position(goal)
            if gx and dist2(px, pz, gx, gz) < (GOAL_R + PL_R) * (GOAL_R + PL_R) then
                _PERSIST.victory = true
                Engine.ui.text("VICTORY! All pickups collected!")
                Engine.log.info("[Dogfood02] VICTORY!")
            end
        end
    end

    -- ── HUD 更新（每帧）──
    local remaining = #pickups - (_PERSIST.score or 0)
    if not _PERSIST.victory then
        if remaining > 0 then
            Engine.ui.text("SCORE: " .. tostring(_PERSIST.score or 0) .. "/" .. tostring(#pickups))
        else
            Engine.ui.text("All collected! Reach the GOAL ZONE!")
        end
    end
end
