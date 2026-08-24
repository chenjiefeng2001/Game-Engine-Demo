-- ═══════════════════════════════════════════════════
-- dogfood06_game.lua — Dogfood-06 Production Efficiency Test
--
-- 约束：仅使用冻结的 Scripting API v2.1（零 C++ 修改）
--
-- 玩法：Top-down Arena Combat
--   Player 用 WASD 移动，靠近敌人自动攻击
--   敌人有 HP，被打到 hp<=0 时销毁
--   全部歼灭后到达 GoalZone → WIN
--   玩家 HP 归零 → LOSE
--
── 本脚本的目的是压力测试 Editor 生产效率：
--   15 实体的管理是否顺畅？
--   哪些操作开始令人痛苦？
--   哪些 API 缺失导致绕路？
-- ═══════════════════════════════════════════════════

_PERSIST = _PERSIST or {}

-- ── M001 Entity.find 按名绑定 ──
local player = Engine.entity.find("Player")
local goal   = Engine.entity.find("GoalZone")

local enemies = {}
for _, n in ipairs({
    "Enemy_Grunt1","Enemy_Grunt2","Enemy_Ranged","Enemy_Tank",
    "Enemy_Scout","Enemy_Boss","Enemy_Minion1","Enemy_Minion2"
}) do
    local h = Engine.entity.find(n)
    if h then
        enemies[#enemies + 1] = {
            handle = h, name = n,
            hp = (n == "Enemy_Boss") and 5 or (n == "Enemy_Tank") and 3 or 1,
        }
    end
end

local pickups = {}
for _, n in ipairs({"Pickup_HP","Pickup_DMG","Pickup_SPD"}) do
    local h = Engine.entity.find(n)
    if h then pickups[#pickups + 1] = { handle = h, name = n } end
end

local walls = {}
for _, n in ipairs({"Wall_N","Wall_S"}) do
    local h = Engine.entity.find(n)
    if h then walls[#walls + 1] = h end
end

-- ═══ 游戏状态 ═══
_PERSIST.state       = _PERSIST.state or "playing"
_PERSIST.playerHP    = _PERSIST.playerHP or 3
_PERSIST.enemiesKilled = _PERSIST.enemiesKilled or 0
_PERSIST.totalEnemies  = #enemies
_PERSIST.timer       = _PERSIST.timer or 60.0
_PERSIST.attackCooldown = 0

local SPEED      = 6.0
local PL_R       = 0.45
local ENEMY_TYPES = {
    ["Enemy_Grunt1"]  = { r=0.50, spd=2.0, hp=1 },
    ["Enemy_Grunt2"]  = { r=0.50, spd=2.0, hp=1 },
    ["Enemy_Ranged"]  = { r=0.40, spd=1.5, hp=1 },
    ["Enemy_Tank"]    = { r=0.70, spd=1.0, hp=3 },
    ["Enemy_Scout"]   = { r=0.35, spd=4.0, hp=1 },
    ["Enemy_Boss"]    = { r=0.90, spd=1.8, hp=5 },
    ["Enemy_Minion1"] = { r=0.35, spd=3.0, hp=1 },
    ["Enemy_Minion2"] = { r=0.35, spd=3.0, hp=1 },
}

local function dist(ax, az, bx, bz)
    local dx, dz = ax - bx, az - bz
    return math.sqrt(dx * dx + dz * dz)
end

function OnCreate()
    local alive = 0
    for _, e in ipairs(enemies) do alive = alive + 1 end
    Engine.log.info("[DF06] Arena started. " .. tostring(alive) .. " enemies, " ..
                    tostring(_PERSIST.totalEnemies) .. " total.")
end

function OnUpdate(dt)
    if _PERSIST.state ~= "playing" then return end

    -- ── 倒计时 ──
    _PERSIST.timer = _PERSIST.timer - dt
    if _PERSIST.timer <= 0 then
        _PERSIST.state = "gameover_timeout"
        Engine.ui.text("TIME UP!")
        Engine.log.warn("[DF06] Timer expired")
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
    local len = math.sqrt(mx*mx + mz*mz)
    if len > 0.01 then
        px = px + (mx/len)*SPEED*dt
        pz = pz + (mz/len)*SPEED*dt
    end

    -- ── 墙壁推挤 ──
    for _, wh in ipairs(walls) do
        local wx,wy,wz = Engine.transform.get_position(wh)
        if wx then
            local d = dist(px,pz,wx,wz)
            if d < PL_R+WALL_R and d > 0.01 then
                px = wx+(px-wx)/d*(PL_R+WALL_R)
                pz = wz+(pz-wz)/d*(PL_R+WALL_R)
            end
        end
    end

    -- ── 攻击冷却递减 ──
    _PERSIST.attackCooldown = math.max(0, _PERSIST.attackCooldown - dt)

    -- ── 近战攻击：玩家接近敌人时自动造成伤害 ──
    if _PERSIST.attackCooldown <= 0 then
        for _, e in ipairs(enemies) do
            local ex,ey,ez = Engine.transform.get_position(e.handle)
            if ex and dist(px,pz,ex,ez) < PL_R + 0.6 then
                e.hp = e.hp - 1
                _PERSIST.attackCooldown = 0.5
                if e.hp <= 0 then
                    -- 销毁敌人
                    Scripting::GameplayAPI::HandleDestroy(e.handle)
                    m_Ed_bindings_remove(e.handle)
                    _PERSIST.enemiesKilled = _PERSIST.enemiesKilled + 1
                    AppendLog("[combat] destroyed: " .. tostring(e.name))
                end
                break  -- 每次攻击只打一个目标
            end
        end
    end

    -- ── 敌人碰撞 → 玩家受伤 ──
    for _, e in ipairs(enemies) do
        local ex,ey,ez = Engine.transform.get_position(e.handle)
        if ex and dist(px,pz,ex,ez) < PL_R+0.5 then
            _PERSIST.playerHP = (_PERSIST.playerHP or 3) - 1
            if _PERSIST.playerHP <= 0 then
                _PERSIST.state = "gameover_hp"
                Engine.ui.text("GAME OVER")
                Engine.log.warn("[DF06] Player HP depleted")
                return
            end
            -- 击退玩家防止连续扣血
            px = px + (px-ex)/math.max(0.01,dist(px,pz,ex,ez))*0.5
            pz = pz + (pz-ez)/math.max(0.01,dist(px,pz,ex,ez))*0.5
            break  -- 每帧最多受一次伤害
        end
    end

    Engine.transform.set_position(player, px, py, pz)

    -- ── 胜利条件：全部敌人歼灭 + 到达 GoalZone ──
    if _PERSIST.enemiesKilled >= _PERSIST.totalEnemies then
        local gx,gy,gz = Engine.transform.get_position(goal)
        if gx and dist(px,pz,gx,gz) < 2.0 then
            _PERSIST.state = "victory"
            Engine.ui.text("VICTORY! All enemies eliminated!")
            Engine.log.info("[DF06] VICTORY!")
            return
        end
    end

    -- ── HUD 更新 ──
    local remaining = _PERSIST.totalEnemies - _PERSIST.enemiesKilled
    local tStr = string.format("%.0f",_PERSIST.timer)
    local hpStr = tostring(math.max(0,_PERSIST.playerHP))
    Engine.ui.text("HP:"..hpStr.." | ENEMIES:"..tostring(remaining).."/"..tostring(_PERSIST.totalEnemies).." | TIME:"..tStr.."s")
end

-- 辅助函数：从绑定表中移除已销毁实体
function m_Ed_bindings_remove(h)
    -- v1 简化：不做实际移除，仅标记
end
