-- ═══════════════════════════════════════════
-- VS01 boss_director.lua — Boss 房控制器
--
-- Boss HP12 缓慢追击，<1/3 HP 狂暴加速（可观测行为变化）
-- 歼灭后 VictoryPortal 激活 → 触碰 → victory
-- ═══════════════════════════════════════════

_PERSIST = _PERSIST or {}

local player = Engine.entity.find("Player")
local portal = Engine.entity.find("VictoryPortal")
local pillars = {}
for _, n in ipairs({"Pillar_W", "Pillar_E", "Pillar_N"}) do
    local h = Engine.entity.find(n)
    if h then pillars[#pillars + 1] = h end
end
local bossH = Engine.entity.find("Boss")

local BOSS = { hp_max = 12, spd = 1.4, spd_enraged = 2.2, r = 0.80, dmg = 2 }
_PERSIST.state   = _PERSIST.state or "playing"
_PERSIST.bossHp  = _PERSIST.bossHp or BOSS.hp_max
_PERSIST.enraged = false

local SPEED     = 4.0
local PL_R      = 0.40
local ATK_RANGE = 1.05
local ATK_CD    = 0.35
local PORTAL_R  = 1.00
local PILLAR_R  = 0.55

local function dist(ax, az, bx, bz)
    local dx, dz = ax - bx, az - bz
    return math.sqrt(dx * dx + dz * dz)
end

function OnCreate()
    Engine.log.info("[VS01-BOSS] entered. carried score=" ..
                    tostring(_PERSIST.score or 0))
end

function OnUpdate(dt)
    if _PERSIST.state ~= "playing" then return end
    if not player then return end
    local px, py, pz = Engine.transform.get_position(player)

    -- 移动
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

    -- 石柱推出
    for _, ph in ipairs(pillars) do
        local qx, qy, qz = Engine.transform.get_position(ph)
        if qx then
            local d = dist(px, pz, qx, qz)
            if d < PL_R + PILLAR_R and d > 0.01 then
                px = qx + (px - qx) / d * (PL_R + PILLAR_R)
                pz = qz + (pz - qz) / d * (PL_R + PILLAR_R)
            end
        end
    end

    -- Boss AI
    if bossH and (_PERSIST.bossHp or 0) > 0 then
        local bx, by, bz = Engine.transform.get_position(bossH)

        -- 攻击 Boss
        _PERSIST.atkCd = math.max(0, (_PERSIST.atkCd or 0) - dt)
        if bx then
            local d = dist(px, pz, bx, bz)
            if d <= ATK_RANGE and (_PERSIST.atkCd or 0) <= 0 then
                _PERSIST.atkCd = ATK_CD
                _PERSIST.bossHp = (_PERSIST.bossHp or 0) - 1
                if (_PERSIST.bossHp or 0) <= 0 then
                    Engine.transform.set_position(bossH, 0, -999, 0)
                    Engine.log.info("[VS01] BOSS DOWN — portal active")
                elseif (_PERSIST.bossHp or 0) <= math.floor(BOSS.hp_max / 3)
                       and not _PERSIST.enraged then
                    _PERSIST.enraged = true
                    Engine.log.warn("[VS01] BOSS ENRAGED")
                end
            end
            -- Boss 追击 + 接触伤害
            if (_PERSIST.bossHp or 0) > 0 and bx then
                local spd = _PERSIST.enraged and BOSS.spd_enraged or BOSS.spd
                local d = dist(px, pz, bx, bz)
                if d > 0.05 then
                    bx = bx + (px - bx) / d * spd * dt
                    bz = bz + (pz - bz) / d * spd * dt
                    Engine.transform.set_position(bossH, bx, by, bz)
                end
                _PERSIST.bossHitCd = math.max(0, (_PERSIST.bossHitCd or 0) - dt)
                if d < BOSS.r + PL_R and (_PERSIST.bossHitCd or 0) <= 0 then
                    _PERSIST.bossHitCd = 0.8
                    _PERSIST.hp = (_PERSIST.hp or 10) - BOSS.dmg
                    if (_PERSIST.hp or 0) <= 0 then
                        _PERSIST.state = "defeated"
                        Engine.ui.text("DEFEATED BY BOSS")
                        return
                    end
                end
            end
        end
    end

    Engine.transform.set_position(player, px, py, pz)

    -- 胜利
    if (_PERSIST.bossHp or 0) <= 0 and portal then
        local vx, vy, vz = Engine.transform.get_position(portal)
        if vx and dist(px, pz, vx, vz) < PORTAL_R then
            _PERSIST.state = "victory"
            Engine.ui.text("VICTORY!")
            Engine.log.info("[VS01] *** VICTORY *** score=" ..
                            tostring(_PERSIST.score))
            return
        end
    end

    Engine.ui.text("BOSS HP:" .. tostring(math.max(0, _PERSIST.bossHp)) ..
                   "/" .. tostring(BOSS.hp_max) ..
                   " YOUR HP:" .. tostring(math.max(0, _PERSIST.hp or 10)))
end
