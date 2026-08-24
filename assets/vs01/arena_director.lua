-- ═══════════════════════════════════════════
-- VS01 arena_director.lua — 主竞技场控制器
--
-- 玩法：清空 5 敌 → Gate 开启 → 触碰 Gate 请求进入 Boss 房
-- 敌人：Chaser(追击) Brute(重甲) Sentry(哨戒炮) Swarm×2(速刷)
-- 全部使用冻结 API v2.1：entity.find/transform/input/ui/log
-- ═══════════════════════════════════════════

_PERSIST = _PERSIST or {}

-- ── 实体绑定 ──
local player = Engine.entity.find("Player")
local gate   = Engine.entity.find("Gate")
local walls  = {}
for _, n in ipairs({"Wall_N", "Wall_S"}) do
    local h = Engine.entity.find(n)
    if h then walls[#walls + 1] = h end
end

local pickups = {
    { name = "P_Hp",     handle = Engine.entity.find("P_Hp"),     kind = "hp",    taken = false },
    { name = "P_Score1", handle = Engine.entity.find("P_Score1"), kind = "score", taken = false },
}

local ENEMY_DEFS = {
    { name = "E_Chaser", hp = 2, spd = 2.2, r = 0.45, dmg = 1 },
    { name = "E_Brute",  hp = 5, spd = 1.2, r = 0.65, dmg = 2 },
    { name = "E_Sentry", hp = 3, spd = 0.0, r = 0.50, dmg = 1 },
    { name = "E_Swarm1", hp = 1, spd = 3.2, r = 0.35, dmg = 1 },
    { name = "E_Swarm2", hp = 1, spd = 3.2, r = 0.35, dmg = 1 },
}
local enemies = {}
for i, d in ipairs(ENEMY_DEFS) do
    local h = Engine.entity.find(d.name)
    if h then
        enemies[#enemies + 1] = { def = d, handle = h, hp = d.hp,
                                  hitCd = 0.0 }
    end
end

-- ── 状态 ──
_PERSIST.state   = _PERSIST.state or "playing"
_PERSIST.hp      = _PERSIST.hp or 10
_PERSIST.score   = _PERSIST.score or 0
_PERSIST.kills   = _PERSIST.kills or 0
_PERSIST.total   = #enemies
_PERSIST.timer   = _PERSIST.timer or 90.0
_PERSIST.gateOpen = false
_PERSIST.request_scene = nil

-- ── 常量（Ledger M002 观察：调参常量驻留脚本）──
local SPEED      = 4.0
local PL_R       = 0.40
local ATK_RANGE  = 0.85
local ATK_CD     = 0.40
local PICKUP_R   = 0.55
local GATE_R     = 1.00
local WALL_R     = 0.60

local function dist(ax, az, bx, bz)
    local dx, dz = ax - bx, az - bz
    return math.sqrt(dx * dx + dz * dz)
end

function OnCreate()
    Engine.log.info("[VS01-ARENA] enemies=" .. tostring(_PERSIST.total) ..
                    " score=" .. tostring(_PERSIST.score))
end

function OnUpdate(dt)
    if _PERSIST.state ~= "playing" then return end

    -- 计时压力（超时即败北变体）
    _PERSIST.timer = _PERSIST.timer - dt
    if _PERSIST.timer <= 0 then
        _PERSIST.state = "defeated_timeout"
        Engine.ui.text("TIME UP — DEFEATED")
        return
    end

    if not player then return end
    local px, py, pz = Engine.transform.get_position(player)

    -- ── 输入移动（WASD，注入后端可驱动）──
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

    -- ── 墙壁推出 ──
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

    -- ── Pickups ──
    for _, pk in ipairs(pickups) do
        if not pk.taken and pk.handle then
            local kx, ky, kz = Engine.transform.get_position(pk.handle)
            if kx and dist(px, pz, kx, kz) < PICKUP_R + PL_R then
                pk.taken = true
                if pk.kind == "hp" then
                    _PERSIST.hp = (_PERSIST.hp or 0) + 2
                else
                    _PERSIST.score = (_PERSIST.score or 0) + 50
                end
                Engine.transform.set_position(pk.handle, 0, -999, 0)
                Engine.log.info("[VS01] pickup: " .. pk.name)
            end
        end
    end

    -- ── 近战攻击：冷却好→命中范围内最近敌人 −1 HP ──
    _PERSIST.atkCd = math.max(0, (_PERSIST.atkCd or 0) - dt)
    local nearest, nd = nil, 1e9
    for _, e in ipairs(enemies) do
        if e.hp > 0 then
            local ex, ey, ez = Engine.transform.get_position(e.handle)
            if ex then
                local d = dist(px, pz, ex, ez)
                if d < nd then nearest, nd = e, d end
            end
        end
    end
    if nearest and nd <= ATK_RANGE and (_PERSIST.atkCd or 0) <= 0 then
        _PERSIST.atkCd = ATK_CD
        nearest.hp = nearest.hp - 1
        if nearest.hp <= 0 then
            _PERSIST.kills = (_PERSIST.kills or 0) + 1
            _PERSIST.score = (_PERSIST.score or 0) + 20
            Engine.transform.set_position(nearest.handle, 0, -999, 0)
            Engine.log.info("[VS01] killed: " .. nearest.def.name ..
                            " (" .. tostring(_PERSIST.kills) .. "/" ..
                            tostring(_PERSIST.total) .. ")")
        end
    end

    -- ── 敌人 AI + 接触伤害 ──
    for _, e in ipairs(enemies) do
        if e.hp > 0 then
            local ex, ey, ez = Engine.transform.get_position(e.handle)
            if ex then
                local d = dist(px, pz, ex, ez)
                if e.def.spd > 0 and d > 0.05 then
                    ex = ex + (px - ex) / d * e.def.spd * dt
                    ez = ez + (pz - ez) / d * e.def.spd * dt
                    Engine.transform.set_position(e.handle, ex, ey, ez)
                end
                e.hitCd = math.max(0, e.hitCd - dt)
                if d < e.def.r + PL_R and e.hitCd <= 0 then
                    e.hitCd = 0.8
                    _PERSIST.hp = (_PERSIST.hp or 0) - e.def.dmg
                    if (_PERSIST.hp or 0) <= 0 then
                        _PERSIST.state = "defeated"
                        Engine.ui.text("DEFEATED")
                        Engine.log.warn("[VS01] player defeated")
                        return
                    end
                end
            end
        end
    end

    Engine.transform.set_position(player, px, py, pz)

    -- ── 清场开门 + 过渡请求 ──
    if (_PERSIST.kills or 0) >= _PERSIST.total then
        if not _PERSIST.gateOpen then
            _PERSIST.gateOpen = true
            Engine.log.info("[VS01] GATE OPEN")
        end
        if gate then
            local gx, gy, gz = Engine.transform.get_position(gate)
            if gx and dist(px, pz, gx, gz) < GATE_R then
                _PERSIST.state = "request_exit"
                _PERSIST.request_scene = "boss"
                Engine.ui.text("ENTERING BOSS ROOM...")
                return
            end
        end
    end

    Engine.ui.text("HP:" .. tostring(math.max(0, _PERSIST.hp)) ..
                   " SCORE:" .. tostring(_PERSIST.score) ..
                   " KILLS:" .. tostring(_PERSIST.kills) .. "/" .. tostring(_PERSIST.total) ..
                   " T:" .. string.format("%.0f", _PERSIST.timer))
end
