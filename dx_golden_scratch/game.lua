_PERSIST = _PERSIST or {}
_PERSIST.initialized = true
function OnCreate()
    _PERSIST.startPos = {x=3,y=0,z=0}
end
function OnUpdate(dt)
    local x,y,z = Engine.transform.get_position(1)
    if x then _PERSIST.lastX = x end
end
