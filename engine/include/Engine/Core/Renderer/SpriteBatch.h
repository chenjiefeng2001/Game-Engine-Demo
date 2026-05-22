#pragma once
#include <memory>
#include <cstdint>
#include <string>

namespace Engine {
    class Texture;


    struct SpriteTransform {
        float x = 0.0f;       
        float y = 0.0f;       
        float angle = 0.0f;   
        float scaleX = 1.0f;  
        float scaleY = 1.0f;  

        // 便捷构造
        static SpriteTransform FromPosition(float px, float py) {
            return { px, py, 0.0f, 1.0f, 1.0f };
        }
        static SpriteTransform FromScale(float px, float py, float sx, float sy) {
            return { px, py, 0.0f, sx, sy };
        }
    };


    struct SpriteData {
        SpriteTransform transform;   
        float uvX = 0.0f;             
        float uvY = 0.0f;             
        float uvW = 1.0f;            
        float uvH = 1.0f;            
        float colorR = 1.0f;          
        float colorG = 1.0f;          
        float colorB = 1.0f;          
        float colorA = 1.0f;          
    };

    class ISpriteBatch {
    public:
        virtual ~ISpriteBatch() = default;


        virtual void Begin(const std::shared_ptr<Texture>& texture) = 0;


        virtual void Draw(const SpriteData& sprite) = 0;

        virtual void End() = 0;

        virtual void Flush() = 0;

        virtual uint32_t GetSpriteCount() const = 0;
        virtual void SetCapacity(uint32_t maxSprites) = 0;
    };

}
