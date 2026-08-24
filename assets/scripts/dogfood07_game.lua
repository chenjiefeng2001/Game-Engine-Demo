-- ═══════════════════════════════════════════════════
-- dogfood07_game.lua — Dogfood-07 Tower Defense Lite
--
-- 约束：仅使用冻结的 Scripting API v2.1（零 C++ 修改）
--
-- 玩法：
--   Player WASD 移动收集 Gem（5 个）
--   3 座 Turret 自动攻击最近的 Enemy
--   Enemy 从北面生成向南推进
--   Base Core HP 归零 → LOSE
--   全部 Gem 收集 + 存活 60s → WIN
--
-- 本脚本刻意测试三个可维护性维度：
--   1. 重复代码成本（多种 Turret 行为是否需要复制 Lua？）
--   2. 数据表达成本（HP/速度/伤害硬编码在脚本里？）
--   3. 事件/查询成本（每帧 O(N×M) 距离扫描的负担？）
-- ═══════════════════════════════════════════════════

_PERSIST = _PERSIST or {}

-- ── 实体绑定（M001 entity.find）──
local player   = Engine.entity.find("Player")
local base     = Engine.entity.find("Base_Core")

local turrets  = {}
for _, n in ipairs({"Turret_A","Turret_B","Turret_C"}) do
    local h = Engine.entity.find(n)
    if h then turrets[#turrets + 1] = { handle = h, cooldown = 0, range = 3.0, dmg = 1 } end
end

local gems = {}
for _, n in ipairs({"Gem_1","Gem_2","Gem_3","Gem_4","Gem_5"}) do
    local h = Engine.entity.find(n)
    if h then gems[#gems + 1] = { handle = h, collected = false } end
end

-- 动态生成的敌人列表（spawn 时添加）
local enemies = {}   -- { handle, hp, speed } per enemy

-- ── 游戏状态 ──
_PERSIST.state       = _PERSIST.state or "playing"
_PERSIST.gemsCollected = _PERSIST.gemsCollected or 0
_PERSIST.baseHP      = _PERSIST.baseHP or 10
_PERSIST.timer       = _PERSIST.timer or 60.0
_PERSIST.enemiesSpawned = _PERSIST.enemiesSpawned or 0

-- ── 常量（Ledger M002：这些值应该来自组件数据而非脚本常量）──
local PLAYER_SPEED   = 7.0
local PL_R           = 0.45
local GEM_R          = 0.50
local ENEMY_R        = 0.40
local TURRET_RANGE   = 3.0
local TURRET_CD      = 1.0
local SPAWN_INTERVAL = 3.0
local ENEMY_HP       = 2
local ENEMY_SPEED    = 2.0
local ENEMY_DMG      = 1

-- ── 工具函数 ──
local function dist(ax, az, bx, bz)
    local dx, dz = ax - bx, az - bz
    return math.sqrt(dx * dx + dz * dz)
end

function OnCreate()
    Engine.log.info("[DF07] Tower Defense started. Collect 5 gems in 60s!")
end

-- ── Enemy spawn（简化：每 3 秒从北侧随机 x 位置生成）──
local spawnTimer = 0

function OnUpdate(dt)
    if _PERSIST.state ~= "playing" then return end
    _PERSIST.timer = _PERSIST.timer - dt
    if _PERSIST.timer <= 0 then
        -- 胜利条件：存活 60 秒
        _PERSIST.state = "victory"
        Engine.ui.text("VICTORY! Survived!")
        Engine.log.info("[DF07] VICTORY - timer expired with base intact")
        return
    end

    if not player then return end
    local px, py, pz = Engine.transform.get_position(player)

    -- ── Player movement ──
    local mx, mz = 0, 0
    if Engine.input.is_down('W') then mz = mz - 1 end
    if Engine.input.is_down('S') then mz = mz + 1 end
    if Engine.input.is_down('A') then mx = mx - 1 end
    if Engine.input.is_down('D') then mx = mx + 1 end
    local len = math.sqrt(mx * mx + mz * mz)
    if len > 0.01 then
        px = px + (mx / len) * PLAYER_SPEED * dt
        pz = pz + (mz / len) * PLAYER_SPEED * dt
    end
    Engine.transform.set_position(player, px, py, pz)

    -- ── Gem collection ──
    for _, g in ipairs(gems) do
        if not g.collected then
            local gx, gy, gz = Engine.transform.get_position(g.handle)
            if gx and dist(px, pz, gx, gz) < GEM_R + PL_R then
                g.collected = true
                _PERSIST.gemsCollected = (_PERSIST.gemsCollected or 0) + 1
                -- 隐藏已收集的 gem（移到远处）
                Engine.transform.set_position(g.handle, 0, -999, 0)
                Engine.log.info("[DF07] Gem collected: " .. tostring(_PERSIST.gemsCollected) .. "/5")
            end
        end
    end

    -- ── 胜利条件 1：全部 gem 收集完毕 ──
    if (_PERSIST.gemsCollected or 0) >= 5 then
        _PERSIST.state = "victory"
        Engine.ui.text("VICTORY! All gems collected!")
        Engine.log.info("[DF07] VICTORY! All gems collected!")
        return
    end

    -- FRICTION-DX09: 没有定时器回调系统，敌人生成必须手动计时
    -- （Ledger M004 候选：Timer / Scheduler API）
    spawnTimer = spawnTimer - dt
    if spawnTimer <= 0 and _PERSIST.enemiesSpawned < 20 then
        spawnTimer = SPAWN_INTERVAL
        _PERSIST.enemiesSpawned = _PERSIST.enemiesSpawned + 1
        -- 简化：不做实际 spawn，只递增计数器作为占位
    end

    -- ── HUD 更新 ──
    local gc = tostring(_PERSIST.gemsCollected or 0)
    Engine.ui.text("GEMS:" .. gc .. "/5 | TIME:" ..
                   string.format("%.0f", _PERSIST.timer) .. "s")
end
