-- ═══════════════════════════════════════════════════
-- dogfood05_game.lua — Dogfood-05 Arena Survival
-- 仅使用冻结的 Scripting API v2.1
-- 玩法：限时生存 + 歼灭，含状态机（playing/gameover/victory）
-- ═══════════════════════════════════════════════════

_PERSIST = _PERSIST or {}
_PERSIST.state = "playing"
_PERSIST.timer = 45.0
_PERSIST.enemiesKilled = 0
_PERSIST.frames = 0

local player = Engine.entity.find("Player")
local chasers = {}
for _, n in ipairs({"Enemy_Alpha","Enemy_Beta","Enemy_Gamma","Enemy_Delta","Enemy_Epsilon"}) do
    local h = Engine.entity.find(n)
    if h then table.insert(chasers, { handle = h, name = n }) end
end

local walls = {}
for _, n in ipairs({"Wall_North","Wall_South","Wall_West","Wall_East"}) do
    local h = Engine.entity.find(n)
    if h then table.insert(walls, h) end
end

local SPEED = 7.0
local CHASE_SPEED = 2.5
local PL_R = 0.45
local CH_R = 0.55
local WALL_R = 0.90

local function dist(ax, az, bx, bz)
    local dx, dz = ax - bx, az - bz
    return math.sqrt(dx * dx + dz * dz)
end

function OnCreate()
    local count = #chasers
    Engine.log.info("[DF05] Started. " .. tostring(count) .. " enemies.")
end

function OnUpdate(dt)
    -- 状态机：非 playing 状态直接返回
    if _PERSIST.state ~= "playing" then return end

    _PERSIST.frames = _PERSIST.frames + 1

    -- 倒计时
    _PERSIST.timer = _PERSIST.timer - dt
    if _PERSIST.timer <= 0 then
        _PERSIST.timer = 0
        _PERSIST.state = "gameover"
        Engine.ui.text("TIME UP!")
        Engine.log.warn("[DF05] Timer expired")
        return
    end

    if not player then return end
    local px, py, pz = Engine.transform.get_position(player)

    -- Player movement
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

    -- Wall collision
    for _, wh in ipairs(walls) do
        local wx, wy, wz = Engine.transform.get_position(wh)
        if wx then
            local d = dist(px, pz, wx, wz)
            local minD = PL_R + WALL_R
            if d < minD and d > 0.01 then
                px = wx + (px - wx) / d * minD
                pz = wz + (pz - wz) / d * minD
            end
        end
    end

    -- Chaser AI movement toward player
    for _, c in ipairs(chasers) do
        local cx, cy, cz2 = Engine.transform.get_position(c.handle)
        if cx and cz2 then
            local dd = dist(px, pz, cx, cz2)
            if dd > 0.3 and dd < 20.0 then
                cx = cx + (px - cx) / dd * CHASE_SPEED * dt
                cz2 = cz2 + (pz - cz2) / dd * CHASE_SPEED * dt
                Engine.transform.set_position(c.handle, cx, cy, cz2)
            end
        end
    end

    -- Collision: chaser touches player → enemy destroyed (simplified model)
    for _, c in ipairs(chasers) do
        local cx, cy, cz2 = Engine.transform.get_position(c.handle)
        if cx and dist(px, pz, cx, cz2) < (PL_R + CH_R) then
            c.alive = false
            _PERSIST.enemiesKilled = _PERSIST.enemiesKilled + 1
            Engine.log.info("[DF05] Enemy destroyed: " .. tostring(c.name))
        end
    end

    Engine.transform.set_position(player, px, py, pz)

    -- Victory: all enemies destroyed
    local aliveN = 0
    for _, c in ipairs(chasers) do
        if c.alive then aliveN = aliveN + 1 end
    end
    if aliveN == 0 and _PERSIST.enemiesKilled > 0 then
        _PERSIST.state = "victory"
        Engine.ui.text("VICTORY!")
        Engine.log.info("[DF05] VICTORY!")
        return
    end

    -- HUD update
    local tStr = string.format("%.1f", _PERSIST.timer)
    Engine.ui.text("ENEMIES:" .. tostring(aliveN) .. "/5 | TIME:" .. tStr .. "s")
end
