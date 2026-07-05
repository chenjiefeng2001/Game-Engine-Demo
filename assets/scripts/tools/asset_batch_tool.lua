-- ============================================================
-- Asset Batch Tool (Lua Script Example)
-- 演示 Lua 脚本系统的功能
-- ============================================================

-- 工具配置
local tool_config = {
    output_format = "png",
    max_resolution = 2048,
    quality = 0.85,
    batch_size = 10
}

-- 打印配置
function PrintConfig()
    print("Asset Batch Tool Configuration:")
    print("  Output Format: " .. tool_config.output_format)
    print("  Max Resolution: " .. tool_config.max_resolution)
    print("  Quality: " .. tool_config.quality)
    print("  Batch Size: " .. tool_config.batch_size)
end

-- 批量处理贴图
function ProcessTextures(input_dir, output_dir)
    print("Processing textures from: " .. input_dir)
    print("Output to: " .. output_dir)
    
    -- 模拟处理
    local count = 0
    for i = 1, tool_config.batch_size do
        count = count + 1
        local progress = (i / tool_config.batch_size) * 100
        print(string.format("  [%d%%] Processing texture %d...", progress, i))
    end
    
    print(string.format("Completed: %d textures processed", count))
    return count
end

-- 计算器工具函数
function Calculate(a, b, operator)
    if operator == "+" then
        return a + b
    elseif operator == "-" then
        return a - b
    elseif operator == "*" then
        return a * b
    elseif operator == "/" then
        if b ~= 0 then
            return a / b
        else
            error("Division by zero!")
            return 0
        end
    else
        error("Unknown operator: " .. operator)
        return 0
    end
end

-- 使用引擎提供的 clamp 和 lerp 函数
function SmoothStep(edge0, edge1, x)
    local t = clamp((x - edge0) / (edge1 - edge0), 0, 1)
    return t * t * (3 - 2 * t)
end

-- 场景工具
function GetSceneInfo()
    local info = {
        name = "Current Scene",
        object_count = 42,
        has_lighting = true,
        ambient_color = { r = 0.02, g = 0.02, b = 0.03 }
    }
    return info
end

-- 调用引擎的 C++ 函数（通过注册暴露）
function LogMessage(level, message)
    if level == "info" then
        print("[INFO] " .. message)
    elseif level == "warn" then
        print("[WARN] " .. message)
    elseif level == "error" then
        error("[ERROR] " .. message)
    end
end

-- 主入口
function Main()
    print("=== Asset Batch Tool (Lua) ===")
    PrintConfig()
    
    local result = ProcessTextures("assets/textures/", "output/textures/")
    print("Result: " .. result .. " textures processed")
    
    -- 测试计算
    local sum = Calculate(10, 5, "+")
    print("10 + 5 = " .. sum)
    
    -- 测试平滑插值
    local smooth = SmoothStep(0, 1, 0.5)
    print("SmoothStep(0, 1, 0.5) = " .. smooth)
    
    print("=== Script completed ===")
    return 0
end

-- 执行主入口
Main()