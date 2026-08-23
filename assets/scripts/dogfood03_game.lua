-- ═══════════════════════════════════════════════════
-- dogfood03_game.lua — Dogfood-03 Dodge & Collect
--
-- 约束：仅使用冻结的 Scripting API v2.1（零 C++ 修改）
--
-- 玩法：
--   30 秒内收集 5 个 Pickup，然后到达 GoalZone → WIN
--   被 Chaser 碰到 → LOSE
--   倒计时归零未收集完 → LOSE
--
-- 实体：13 个（全部由场景 JSON 提供）
-- 脚本：单 ScriptInstance 驱动全部实体（v1 架构约束）
-- ═══════════════════════════════════════════════════

_PERSIST = _PERSIST or {}

-- ═══ 初始化：按名查找所有实体（M001 entity.find）═══
local player   = Engine.entity.find("Player")
local goal     = Engine.entity.find("GoalZone")
local chasers  = {}
for _, n in ipairs({"Chaser1", "Chaser2"}) do
    local h = Engine.entity.find(n)
    if h then chasers[#chasers + 1] = h end
end

local pickups = {}
for _, n in ipairs({"Pickup_A","Pickup_B","Pickup_C","Pickup_D","Pickup_E"}) do
    local h = Engine.entity.find(n)
    if h then pickups[#pickups + 1] = { handle = h, name = n, taken = false } end
end

local walls = {}
for _, n in ipairs({"Wall_Left","Wall_Right","Wall_Top"}) do
    local h = Engine.entity.find(n)
    if h then walls[#walls + 1] = h end
end

-- ═══ 游戏状态 ═══
_PERSIST.score     = _PERSIST.score or 0
_PERSIST.gameState = _PERSIST.gameState or "playing"   -- playing / won / lost
_PERSIST.timer     = _PERSIST.timer or 30.0            -- 秒
_PERSIST.frames    = _PERSIST.frames or 0

-- ═══ 常量 ═══
local SPEED       = 7.0
local CHASE_SPEED = 3.5
local PL_R        = 0.45
local CH_R        = 0.55
local PK_R        = 0.50
local WALL_R      = 0.90
local GOAL_R      = 1.2
local TOTAL_PK    = #pickups

-- ═══ 工具函数 ═══
local function dist2(ax, az, bx, bz)
    local dx, dz = ax - bx, az - bz
    return dx * dx + dz * dz
end

local function dist(ax, az, bx, bz)
    return math.sqrt(dist2(ax, az, bx, bz))
end

-- ═══ OnCreate ═══
function OnCreate()
    Engine.ui.text("Collect 5 pickups! Avoid red chasers!")
    Engine.log.info("[DF03] Game started: " .. tostring(TOTAL_PK) .. " pickups, " ..
                    tostring(#chasers) .. " chasers, 30s timer")
end

-- ═══ OnUpdate ═══
function OnUpdate(dt)
    if _PERSIST.gameState ~= "playing" then return end
    _PERSIST.frames = _PERSIST.frames + 1

    -- ── 倒计时 ──
    _PERSIST.timer = _PERSIST.timer - dt
    if _PERSIST.timer <= 0 then
        _PERSIST.timer = 0
        _PERSIST.gameState = "lost"
        Engine.ui.text("TIME UP! You lose.")
        Engine.log.warn("[DF03] TIME UP - player failed to collect in time")
        return
    end

    if not player then return end
    local px, py, pz = Engine.transform.get_position(player)

    -- ── 玩家移动 ──
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

    -- ── Chaser AI：向玩家方向移动 ──
    for _, ch in ipairs(chasers) do
        local cx, cy, cz2 = Engine.transform.get_position(ch)
        if cx then
            local d = dist(px, pz, cx, cz2)
            if d > 0.3 then
                cx = cx + (px - cx) / d * CHASE_SPEED * dt
                cz2 = cz2 + (pz - cz2) / d * CHASE_SPEED * dt
                Engine.transform.set_position(ch, cx, cy, cz2)
            end
        end
    end

    -- ── Chaser 碰撞检测 → LOSE ──
    for _, ch in ipairs(chasers) do
        local cx, cy, cz2 = Engine.transform.get_position(ch)
        if cx and dist2(px, pz, cx, cz2) < (PL_R + CH_R) * (PL_R + CH_R) then
            _PERSIST.gameState = "lost"
            Engine.ui.text("CAUGHT! Game Over.")
            Engine.log.warn("[DF03] Player caught by chaser!")
            return
        end
    end

    -- ── 墙壁推挤 ──
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

    -- ── Pickup 收集 ──
    for _, pk in ipairs(pickups) do
        if not pk.taken then
            local sx, sy, sz = Engine.transform.get_position(pk.handle)
            if sx and dist2(px, pz, sx, sz) < (PL_R + PK_R) * (PL_R + PK_R) then
                pk.taken = true
                _PERSIST.score = _PERSIST.score + 1
                Engine.log.info("[DF03] Collected " .. pk.name ..
                    " (" .. tostring(_PERSIST.score) .. "/" .. tostring(TOTAL_PK) .. ")")
            end
        end
    end

    Engine.transform.set_position(player, px, py, pz)

    -- ── 全部收集完毕 → 提示去 GoalZone ──
    if _PERSIST.score >= TOTAL_PK and not goal == nil then
        local gx, gy, gz = Engine.transform.get_position(goal)
        if gx and dist2(px, pz, gx, gz) < (GOAL_R + PL_R) * (GOAL_R + PL_R) then
            _PERSIST.gameState = "won"
            Engine.ui.text("VICTORY! Score: " .. tostring(_PERSIST.score))
            Engine.log.info("[DF03] VICTORY! All pickups + reached goal!")
        end
    end

    -- ── HUD 更新 ──
    local remaining = TOTAL_PK - _PERSIST.score
    local timerStr = string.format("%.1f", _PERSIST.timer)
    if remaining > 0 then
        Engine.ui.text("SCORE:" .. tostring(_PERSIST.score) .. "/" .. tostring(TOTAL_PK) ..
                       " | TIME:" .. timerStr .. "s")
    else
        Engine.ui.text("ALL COLLECTED! Reach GOAL! | " .. timerStr .. "s left")
    end
end
