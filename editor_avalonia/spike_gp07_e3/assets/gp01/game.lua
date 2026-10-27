-- ═══════════════════════════════════════════
-- GP01 game.lua — Main 场景导演（GP1-C Content Scale）
--
-- 规模形态：场景仅含 Player/Walls/SpawnPads/Director；
-- 敌人全部运行时经 Engine.entity.spawn 生成（冻结 API），5 波编成 ≥30 体。
-- 存档契约：场景文件=静态布局资产；敌人/波次/计数等运行时状态全部由
-- GameStateEncode/Restore 承载（宿主经 _PERSIST 中介）——读档后按存档
-- 坐标重铸幸存者，亡者保持墓碑。
-- 全部使用冻结 API：entity.find/spawn/transform/input/ui/log
-- ═══════════════════════════════════════════

_PERSIST = _PERSIST or {}

-- ── 敌人数据模型（Lua 数据表 = 模块契约；boss 为 GP1-C 新增第四型）──
ENEMY_TYPES = {
    grunt = { hp = 3,  maxHp = 3,  speed = 1.0,  damage = 1, radius = 0.45, value = 20 },
    tank  = { hp = 8,  maxHp = 8,  speed = 0.45, damage = 2, radius = 0.65, value = 50 },
    scout = { hp = 2,  maxHp = 2,  speed = 1.6,  damage = 1, radius = 0.35, value = 15 },
    boss  = { hp = 16, maxHp = 16, speed = 0.5,  damage = 3, radius = 0.80, value = 100 },
}

-- ── 波次编成（纯数据；五波共 32 体：19 grunt / 6 tank / 7 scout）──
WAVES = {
    { "grunt", "grunt", "grunt", "grunt", "grunt" },
    { "grunt", "grunt", "grunt", "scout", "scout" },
    { "tank", "tank", "tank", "grunt", "grunt",
      "grunt", "grunt", "grunt" },
    { "tank", "tank", "scout", "scout", "scout", "scout" },
    { "tank", "scout", "grunt", "grunt", "grunt",
      "grunt", "grunt", "grunt" },
}

-- ── 实体绑定 ──
local player = Engine.entity.find("Player")
local walls = {}
for _, n in ipairs({"Wall_N", "Wall_S", "Wall_W", "Wall_E"}) do
    local h = Engine.entity.find(n)
    if h then walls[#walls + 1] = h end
end
local pads = {}
for _, n in ipairs({"Pad_N", "Pad_S", "Pad_W", "Pad_E"}) do
    local h = Engine.entity.find(n)
    if h then pads[#pads + 1] = h end
end

-- ── 运行时账簿（GP-006 观察位：动态生成后由游戏代码自维护）──
local enemies = {}                          -- {name,type,def,handle,hp,maxHp,alive,hitCd}
STATS = { spawned = 0, destroyed = 0 }      -- 遥测

-- ── 状态 ──
_PERSIST.state      = _PERSIST.state or "playing"
_PERSIST.hp         = _PERSIST.hp or 14
_PERSIST.score      = _PERSIST.score or 0
_PERSIST.waveIdx    = _PERSIST.waveIdx or 0          -- 已生成的最大波次
_PERSIST.startGrace = _PERSIST.startGrace or 2.0     -- 开局宽限（波1前）

-- ── 常量 ──
local SPEED      = 4.0
local PL_R       = 0.40
local WALL_R     = 0.60
local ATK_DMG    = 1
local ATK_CD     = 0.35
local ATK_RANGE  = 1.10
local CONTACT_CD = 1.00
local BASE_SPEED = 2.2

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

-- ── 物化：生成实体并登记账簿（SpawnAt 与 Restore 共用底层）──
local function Materialize(name, typeName, hp, x, z)
    local t = ENEMY_TYPES[typeName]
    if not t then return nil end
    local h = Engine.entity.spawn(name)               -- ← 冻结 API
    if not h then return nil end
    Engine.transform.set_position(h, x, 0, z)
    local e = { name = name, type = typeName, def = t, handle = h,
                hp = hp, maxHp = t.maxHp, alive = true, hitCd = 0 }
    enemies[#enemies + 1] = e
    return e
end

-- SpawnAt(typeName) -> name|nil
--   落点：4 个 Pad 轮转 + 确定性散布；命名 E_001..E_NNN 全局唯一。
--   ★ GP-006 实验：账簿（enemies 表 + 命名计数器）完全由本文件维护，
--     引擎不提供"枚举已生成实体"的手段 —— 此即被测量的生产成本。
function SpawnAt(typeName)
    local t = ENEMY_TYPES[typeName]
    if not t then return nil end
    STATS.spawned = STATS.spawned + 1
    local name = string.format("E_%03d", STATS.spawned)
    local pad = pads[(STATS.spawned - 1) % #pads + 1]
    local px, py, pz = Engine.transform.get_position(pad)
    local k = (STATS.spawned % 5) - 2                 -- −2..2 散布
    local e = Materialize(name, typeName, t.hp,
                          px + k * 0.22, pz + k * 0.13)
    if not e then STATS.spawned = STATS.spawned - 1; return nil end
    return name
end

-- SpawnWave(idx) -> bool：按编成生成一整波
function SpawnWave(idx)
    if idx > #WAVES then return false end
    _PERSIST.waveIdx = idx
    for _, tn in ipairs(WAVES[idx]) do SpawnAt(tn) end
    Engine.log.info("[GP01] wave " .. tostring(idx) .. "/" ..
                    tostring(#WAVES) .. " spawned: " ..
                    tostring(#WAVES[idx]))
    return true
end

-- ── 存档编解码 v3 ──
-- 头部：score|hp|state|waveIdx|startGrace|spawned|destroyed
-- 条目：name:type:hp:alive:x:z （亡者为墓碑条目，x/z 记 0）
function GameStateEncode()
    local parts = {
        tostring(_PERSIST.score), tostring(_PERSIST.hp),
        tostring(_PERSIST.state), tostring(_PERSIST.waveIdx),
        tostring(_PERSIST.startGrace), tostring(STATS.spawned),
        tostring(STATS.destroyed),
    }
    for _, e in ipairs(enemies) do
        local x, z = 0.0, 0.0
        if e.alive and e.handle then
            local ex, ey, ez = Engine.transform.get_position(e.handle)
            if ex then x, z = ex, ez end
        end
        parts[#parts + 1] = string.format("%s:%s:%d:%d:%.3f:%.3f",
            e.name, e.type, e.hp, e.alive and 1 or 0, x, z)
    end
    return table.concat(parts, "|")
end

local function SplitField(s, sep, i)
    local j = string.find(s, sep, i, true)
    local f = string.sub(s, i, j and (j - 1) or nil)
    return f, j and (j + 1) or (#s + 1)
end

function GameStateRestore(s)
    local i = 1
    local function nextField() local f, ni = SplitField(s, "|", i); i = ni; return f end
    _PERSIST.score      = tonumber(nextField())
    _PERSIST.hp         = tonumber(nextField())
    _PERSIST.state      = nextField()
    _PERSIST.waveIdx    = tonumber(nextField())
    _PERSIST.startGrace = tonumber(nextField())
    STATS.spawned       = tonumber(nextField())
    STATS.destroyed     = tonumber(nextField())
    enemies = {}
    while i <= #s do
        local entry = nextField()
        if entry ~= "" then
            -- 手工分段：name:type:hp:alive:x:z
            local c1 = string.find(entry, ":", 1, true)
            local c2 = string.find(entry, ":", c1 + 1, true)
            local c3 = string.find(entry, ":", c2 + 1, true)
            local c4 = string.find(entry, ":", c3 + 1, true)
            local c5 = string.find(entry, ":", c4 + 1, true)
            local ename = string.sub(entry, 1, c1 - 1)
            local etype = string.sub(entry, c1 + 1, c2 - 1)
            local ehp   = tonumber(string.sub(entry, c2 + 1, c3 - 1))
            local ealv  = string.sub(entry, c3 + 1, c4 - 1) == "1"
            local exs   = tonumber(string.sub(entry, c4 + 1, c5 - 1))
            local ezs   = tonumber(string.sub(entry, c5 + 1))
            if ealv then
                local e = Materialize(ename, etype, ehp, exs, ezs)
                if not e then
                    enemies[#enemies + 1] = {
                        name = ename, type = etype,
                        def = ENEMY_TYPES[etype], handle = nil,
                        hp = ehp, maxHp = ENEMY_TYPES[etype].maxHp,
                        alive = false, hitCd = 0 }
                end
            else
                enemies[#enemies + 1] = {
                    name = ename, type = etype,
                    def = ENEMY_TYPES[etype], handle = nil,
                    hp = 0, maxHp = ENEMY_TYPES[etype].maxHp,
                    alive = false, hitCd = 0 }
            end
        end
    end
end

-- ── 攻击（M004 观察位：is_down+冷却 = 单击一刀 / 按住连击）──
local function TryAttack()
    if (_PERSIST.atkCd or 0) > 0 then return end
    local px, py, pz = Engine.transform.get_position(player)
    local nearest, nd = nil, 1e9
    for _, e in ipairs(enemies) do
        if e.alive and e.handle then
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
            STATS.destroyed = STATS.destroyed + 1
            _PERSIST.score = (_PERSIST.score or 0) + nearest.def.value
            Engine.entity.destroy(nearest.handle)      -- 尸体即刻消亡
            nearest.handle = nil
            Engine.log.info("[GP01] killed: " .. nearest.name ..
                            " +" .. tostring(nearest.def.value))
        end
    end
end

function OnCreate()
    Engine.log.info("[GP01] content-scale director: waves=" ..
                    tostring(#WAVES) .. " pads=" .. tostring(#pads))
end

function OnUpdate(dt)
    if _PERSIST.state ~= "playing" then return end
    if not player then return end

    local px, py, pz = Engine.transform.get_position(player)

    -- 输入移动 + 边界推出
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

    -- ── 波次推进：宽限 → 清场触发下一波（Timer/Scheduler 观察位：
    --    若改为墙钟节奏波，此处需要手工维护多个计时器）──
    if (_PERSIST.startGrace or 0) > 0 then
        _PERSIST.startGrace = _PERSIST.startGrace - dt
    elseif (_PERSIST.waveIdx or 0) < #WAVES and AliveCount() == 0 then
        SpawnWave((_PERSIST.waveIdx or 0) + 1)
    end

    -- ── 敌人 AI + 接触伤害（含墓碑跳过）──
    for _, e in ipairs(enemies) do
        if e.alive and e.handle then
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

    -- ── 攻击 ──
    _PERSIST.atkCd = math.max(0, (_PERSIST.atkCd or 0) - dt)
    if Engine.input.is_down('J') then TryAttack() end

    -- ── 胜利：末波清场 ──
    if (_PERSIST.waveIdx or 0) >= #WAVES and AliveCount() == 0
        and STATS.spawned > 0 then
        _PERSIST.state = "victory"
        Engine.ui.text("VICTORY")
        return
    end

    Engine.ui.text("HP:" .. tostring(math.max(0, _PERSIST.hp)) ..
                   " SCORE:" .. tostring(_PERSIST.score) ..
                   " WAVE:" .. tostring(math.min(_PERSIST.waveIdx + 1, #WAVES)) ..
                   "/" .. tostring(#WAVES) ..
                   " ALIVE:" .. tostring(AliveCount()))
end
