-- ═══════════════════════════════════════════
-- VS01 menu_director.lua — 主菜单控制器
--
-- 按 RETURN（或注入）请求进入 Arena
-- ═══════════════════════════════════════════

_PERSIST = _PERSIST or {}
_PERSIST.state = _PERSIST.state or "menu"

local pad = Engine.entity.find("StartPad")

function OnCreate()
    Engine.ui.text("ARENA TRIALS — press RETURN to start")
    Engine.log.info("[VS01-MENU] ready")
end

function OnUpdate(dt)
    if _PERSIST.state ~= "menu" then return end
    if Engine.input.is_down('RETURN') then
        _PERSIST.state = "request_start"
        _PERSIST.request_scene = "arena"
        Engine.log.info("[VS01-MENU] start requested")
    end
end
