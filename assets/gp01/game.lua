-- ═══════════════════════════════════════════
-- GP01 game.lua — Main 场景导演（GP1-B Core Gameplay）
--
-- 核心循环（docs/GP-P1-Charter.md §7 / GP1-B 方案）：
--   移动 → 遭遇 → 追击 → 攻击 → HP↓ → 死亡 → Score↑ → 清场 → Victory
-- 敌人差异全部来自 ENEMY_TYPES 数据表（非引擎组件，M003/M002 纪律）。
-- 全部使用冻结 API：entity.find/transform/input/ui/log
--
-- 已知契约约束：
--   GP-001 沙箱无 dofile/require → 单文件组织。
--   战斗状态存续 = 场景序列化(位置) + 宿主中介 _PERSIST 编码串
--   （GameStateEncode/Restore，格式由本文件拥有 —— M002 替代证据）。
-- ═══════════════════════════════════════════

_PERSIST = _PERSIST or {}

-- ── 敌人数据模型（第一版：Lua 数据表，不是引擎组件）
--    全局暴露：数据表即模块契约（GP-001 单文件约束下的分区手段）──
ENEMY_TYPES = {
    grunt = { hp = 3, maxHp = 3,  speed = 1.0,  damage = 1, radius = 0.45, value = 20 },
    tank  = { hp = 8, maxHp = 8,  speed = 0.45, damage = 2, radius = 0.65, value = 50 },
    scout = { hp = 2, maxHp = 2,  speed = 1.6,  damage = 1, radius = 0.35, value = 15 },
}

-- ── 场面配置（名字 → 类型）──
local ENEMY_ROSTER = {
    { name = "E_Grunt1", type = "grunt" },
    { name = "E_Grunt2", type = "grunt" },
    { name = "E_Tank1",  type = "tank"  },
    { name = "E_Scout1", type = "scout" },
    { name = "E_Scout2", type = "scout" },
}

-- ── 实体绑定（运行时状态逐实例独立）──
local player = Engine.entity.find("Player")
local walls = {}
for _, n in ipairs({"Wall_N", "Wall_S", "Wall_W", "Wall_E"}) do
    local h = Engine.entity.find(n)
    if h then walls[#walls + 1] = h end
end

local enemies = {}
for _, r in ipairs(ENEMY_ROSTER) do
    local h = Engine.entity.find(r.name)
    if h then
        local t = ENEMY_TYPES[r.type]
        enemies[#enemies + 1] = {
            name = r.name, def = t, handle = h,
            hp = t.hp, maxHp = t.maxHp,      -- 独立实例状态
            alive = true, hitCd = 0.0,
        }
    end
end

-- ── 状态 ──
_PERSIST.state = _PERSIST.state or "playing"
_PERSIST.hp    = _PERSIST.hp or 14
_PERSIST.score = _PERSIST.score or 0

-- ── 常量（调参驻留脚本 —— M002 观察位）──
local SPEED      = 4.0     -- 玩家
local PL_R       = 0.40
local WALL_R     = 0.60
local ATK_DMG    = 1
local ATK_CD     = 0.35
local ATK_RANGE  = 1.10    -- 覆盖 tank 接触半径 1.05
local CONTACT_CD = 1.00
local BASE_SPEED = 2.2     -- 敌人基础速度 × 类型倍率

local function dist(ax, az, bx, bz)
    local dx, dz = ax - bx, az - bz
    return math.sqrt(dx * dx + dz * dz)
end

local function AliveCount()
    local n = 0
    for _, e in ipairs(enemies) do
        if e.alive then n = n + 1 end
    end
    return n
end

-- ── 存档编解码（宿主经 _PERSIST 中介；格式归本文件所有）──
function GameStateEncode()
    local parts = {
        tostring(_PERSIST.score), tostring(_PERSIST.hp),
        tostring(_PERSIST.state),
    }
    for _, e in ipairs(enemies) do
        parts[#parts + 1] = e.name .. ":" .. tostring(e.hp) .. ":" ..
                            (e.alive and "1" or "0")
    end
    return table.concat(parts, "|")
end

function GameStateRestore(s)
    local i = 1
    local function nextField(sep)
        local j = string.find(s, sep, i, true)
        local f = string.sub(s, i, j and (j - 1) or nil)
        i = j and (j + 1) or (#s + 1)
        return f
    end
    _PERSIST.score = tonumber(nextField("|"))
    _PERSIST.hp    = tonumber(nextField("|"))
    _PERSIST.state = nextField("|")
    while i <= #s do
        local entry = nextField("|")
        local n1 = string.find(entry, ":", 1, true)
        local n2 = string.find(entry, ":", n1 + 1, true)
        local ename = string.sub(entry, 1, n1 - 1)
        local ehp   = tonumber(string.sub(entry, n1 + 1, n2 - 1))
        local ealive = string.sub(entry, n2 + 1) == "1"
        for _, e in ipairs(enemies) do
            if e.name == ename then e.hp = ehp; e.alive = ealive end
        end
    end
end

-- ── 攻击输入观察（M004）：is_down('J') + 冷却门控即天然实现
--    "按一下打一次、按住按冷却连击"，无需引擎边沿 API。
local function TryAttack()
    if (_PERSIST.atkCd or 0) > 0 then return end
    local px, py, pz = Engine.transform.get_position(player)
    local nearest, nd = nil, 1e9
    for _, e in ipairs(enemies) do
        if e.alive then
            local ex, ey, ez = Engine.transform.get_position(e.handle)
            if ex then
                local d = dist(px, pz, ex, ez)
                if d < nd then nearest, nd = e, d end
            end
        end
    end
    if nearest and nd <= ATK_RANGE then
        _PERSIST.atkCd = ATK_CD
        nearest.hp = nearest.hp - ATK_DMG
        if nearest.hp <= 0 then
            nearest.alive = false
            nearest.hp = 0
            _PERSIST.score = (_PERSIST.score or 0) + nearest.def.value
            Engine.log.info("[GP01] killed: " .. nearest.name ..
                            " +" .. tostring(nearest.def.value))
        end
    end
end

function OnCreate()
    Engine.log.info("[GP01] core gameplay: enemies=" .. tostring(#enemies) ..
                    " alive=" .. tostring(AliveCount()))
end

function OnUpdate(dt)
    if _PERSIST.state ~= "playing" then return end
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

    -- ── 敌人 AI：直线追击（差异仅来自数据表）+ 接触伤害 ──
    for _, e in ipairs(enemies) do
        if e.alive then
            local ex, ey, ez = Engine.transform.get_position(e.handle)
            if ex then
                local d = dist(px, pz, ex, ez)
                local spd = e.def.speed * BASE_SPEED
                if d > 0.05 and d > e.def.radius + PL_R then
                    ex = ex + (px - ex) / d * spd * dt
                    ez = ez + (pz - ez) / d * spd * dt
                    Engine.transform.set_position(e.handle, ex, ey, ez)
                end
                e.hitCd = math.max(0, e.hitCd - dt)
                if d < e.def.radius + PL_R and e.hitCd <= 0 then
                    e.hitCd = CONTACT_CD
                    _PERSIST.hp = (_PERSIST.hp or 0) - e.def.damage
                    if (_PERSIST.hp or 0) <= 0 then
                        _PERSIST.state = "lost"
                        Engine.ui.text("GAME OVER")
                        Engine.log.warn("[GP01] player lost")
                        return
                    end
                end
            end
        end
    end

    Engine.transform.set_position(player, px, py, pz)

    -- ── 玩家攻击（M004 观察位：is_down + 冷却即可用）──
    _PERSIST.atkCd = math.max(0, (_PERSIST.atkCd or 0) - dt)
    if Engine.input.is_down('J') then TryAttack() end

    -- ── 胜负判定 ──
    if AliveCount() == 0 then
        _PERSIST.state = "victory"
        Engine.ui.text("VICTORY")
        return
    end

    Engine.ui.text("HP:" .. tostring(math.max(0, _PERSIST.hp)) ..
                   " SCORE:" .. tostring(_PERSIST.score) ..
                   " ENEMIES:" .. tostring(AliveCount()))
end
