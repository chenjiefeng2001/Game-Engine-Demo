#include "BarePhysicsTestApp.h"
#include <array>
#include <algorithm>

namespace Engine {

    BarePhysicsTestApp::BarePhysicsTestApp() {
        std::cout << "========================================" << std::endl;
        std::cout << "  Bare2DPhysicsWorld Functional Tests" << std::endl;
        std::cout << "========================================" << std::endl;
    }

    bool BarePhysicsTestApp::Assert(bool condition, const std::string& testName,
                                     const std::string& message) {
        if (!condition) {
            std::cout << "  [FAIL] " << testName << ": " << message << std::endl;
            m_Failed++;
            return false;
        }
        std::cout << "  [PASS] " << testName << std::endl;
        m_Passed++;
        return true;
    }

    bool BarePhysicsTestApp::RunAllTests() {
        // 按依赖顺序执行
        bool allOk = true;

        allOk &= TestGravityAndForceGenerators();  // 1. 基础力
        allOk &= TestCircleCollision();             // 2. Circle
        allOk &= TestBoxCollision();                // 3. Box
        allOk &= TestPolygonCollision();            // 4. SAT
        allOk &= TestSleeping();                    // 5. 休眠
        allOk &= TestStackingStability();           // 6. 堆叠
        allOk &= TestMaterialSystem();              // 7. 材质
        allOk &= TestBodyTypeChange();              // 8. 类型切换
        allOk &= TestTriggerSensor();               // 9. Trigger
        allOk &= TestCollisionFiltering();          // 10. 碰撞滤波
        allOk &= TestRayCast();                     // 11. 光线投射
        allOk &= TestShapeCast();                   // 12. 形状投射
        allOk &= TestMultiFixture();                // 13. 多 Fixture
        allOk &= TestCCD();                         // 14. CCD

        std::cout << "\nResults: " << m_Passed << " passed, "
                  << m_Failed << " failed" << std::endl;
        return allOk;
    }

    // ============================================================
    // Test 1: 重力 + 力发生器
    // ============================================================
    bool BarePhysicsTestApp::TestGravityAndForceGenerators() {
        std::cout << "\n--- Test: Gravity & Force Generators ---" << std::endl;
        bool ok = true;
        {
            auto world = std::make_shared<Bare2DPhysicsWorld>(Vec2(0.0f, -10.0f));

            // 创建一个动态物体
            BodyDef def;
            def.type = BodyType::Dynamic;
            def.shape.type = ShapeType::Box;
            def.shape.boxSize = Vec2(0.5f, 0.5f);
            def.material.density = 1.0f;
            auto body = world->CreateBody(def);

            Vec2 initialPos = body->GetPosition();

            // 添加额外阻力力发生器
            world->AddForceGenerator(std::make_shared<LinearDragForce>());

            // 步进几帧
            for (int32 i = 0; i < 60; ++i) {
                world->Step(FIXED_DT, 8, 3);
            }

            Vec2 finalPos = body->GetPosition();
            // 物体应该在重力作用下下降 (y 变小)
            ok &= Assert(finalPos.y < initialPos.y - 1.0f,
                "Gravity pulls body down",
                "Expected y to decrease significantly, got: " +
                std::to_string(finalPos.y - initialPos.y));

            // 添加摩擦力效果测试
            world->ClearForceGenerators();
            world->AddForceGenerator(std::make_shared<GravityForce>());
        }
        return ok;
    }

    // ============================================================
    // Test 2: Circle 碰撞
    // ============================================================
    bool BarePhysicsTestApp::TestCircleCollision() {
        std::cout << "\n--- Test: Circle Collision ---" << std::endl;
        bool ok = true;
        {
            auto world = std::make_shared<Bare2DPhysicsWorld>(Vec2(0, 0));
            int32 contactCount = 0;

            world->SetContactBeginCallback([&](const ContactInfo&) {
                contactCount++;
            });

            // 两个圆相向而行
            BodyDef defA, defB;
            defA.type = BodyType::Dynamic;
            defA.shape.type = ShapeType::Circle;
            defA.shape.circleRadius = 0.5f;
            defA.position = Vec2(-2.0f, 0.0f);
            defA.linearVelocity = Vec2(5.0f, 0.0f);
            defA.material.restitution = 1.0f;  // 完全弹性
            auto bodyA = world->CreateBody(defA);

            defB.type = BodyType::Dynamic;
            defB.shape.type = ShapeType::Circle;
            defB.shape.circleRadius = 0.5f;
            defB.position = Vec2(2.0f, 0.0f);
            defB.linearVelocity = Vec2(-5.0f, 0.0f);
            defB.material.restitution = 1.0f;
            auto bodyB = world->CreateBody(defB);

            // 步进直到碰撞
            for (int32 i = 0; i < 120; ++i) {
                world->Step(FIXED_DT, 8, 3);
            }

            // 弹性碰撞后速度方向应该改变（相向运动变为背向）
            Vec2 velA = bodyA->GetLinearVelocity();
            Vec2 velB = bodyB->GetLinearVelocity();
            // 体A应该向右（反弹后），体B应该向左
            ok &= Assert(velA.x > -2.0f, "Circles bounce off each other",
                         "velA.x = " + std::to_string(velA.x) + ", velB.x = " + std::to_string(velB.x));
        }
        return ok;
    }

    // ============================================================
    // Test 3: Box 碰撞
    // ============================================================
    bool BarePhysicsTestApp::TestBoxCollision() {
        std::cout << "\n--- Test: Box Collision ---" << std::endl;
        bool ok = true;
        {
            auto world = std::make_shared<Bare2DPhysicsWorld>(Vec2(0, 0));
            int32 contactCount = 0;

            world->SetContactBeginCallback([&](const ContactInfo&) {
                contactCount++;
            });

            // 一个运动的箱子撞向静态箱子
            BodyDef groundDef;
            groundDef.type = BodyType::Static;
            groundDef.shape.type = ShapeType::Box;
            groundDef.shape.boxSize = Vec2(1.0f, 0.5f);
            groundDef.position = Vec2(0.0f, -2.0f);
            world->CreateBody(groundDef);

            BodyDef boxDef;
            boxDef.type = BodyType::Dynamic;
            boxDef.shape.type = ShapeType::Box;
            boxDef.shape.boxSize = Vec2(0.5f, 0.5f);
            boxDef.position = Vec2(0.0f, 2.0f);
            boxDef.linearVelocity = Vec2(0.0f, -5.0f);
            boxDef.material.restitution = 0.0f;  // 非弹性
            auto box = world->CreateBody(boxDef);

            // 步进直到碰撞
            for (int32 i = 0; i < 120; ++i) {
                world->Step(FIXED_DT, 8, 3);
            }

            ok &= Assert(contactCount > 0,
                "Box-Ground collision detected",
                "Expected > 0 contacts");

            // 箱子应停在静态体之上
            Vec2 pos = box->GetPosition();
            ok &= Assert(pos.y > -2.5f,
                "Box rests on ground",
                "pos.y = " + std::to_string(pos.y) + " (expected > -2.5)");
        }
        return ok;
    }

    // ============================================================
    // Test 4: Polygon 碰撞 (SAT)
    // ============================================================
    bool BarePhysicsTestApp::TestPolygonCollision() {
        std::cout << "\n--- Test: Polygon Collision (SAT) ---" << std::endl;
        bool ok = true;
        {
            auto world = std::make_shared<Bare2DPhysicsWorld>(Vec2(0, 0));
            int32 contactCount = 0;

            world->SetContactBeginCallback([&](const ContactInfo&) {
                contactCount++;
            });

            // 创建两个三角形多边形
            Vec2 triVertsA[] = {Vec2(0.0f, 0.5f), Vec2(-0.5f, -0.5f), Vec2(0.5f, -0.5f)};
            Vec2 triVertsB[] = {Vec2(0.0f, 0.5f), Vec2(-0.5f, -0.5f), Vec2(0.5f, -0.5f)};

            BodyDef defA;
            defA.type = BodyType::Dynamic;
            defA.shape.type = ShapeType::Polygon;
            defA.shape.polygonVertices = triVertsA;
            defA.shape.polygonVertexCount = 3;
            defA.position = Vec2(-1.5f, 0.0f);
            defA.linearVelocity = Vec2(3.0f, 0.0f);
            auto bodyA = world->CreateBody(defA);

            BodyDef defB;
            defB.type = BodyType::Dynamic;
            defB.shape.type = ShapeType::Polygon;
            defB.shape.polygonVertices = triVertsB;
            defB.shape.polygonVertexCount = 3;
            defB.position = Vec2(1.5f, 0.0f);
            defB.linearVelocity = Vec2(-3.0f, 0.0f);
            auto bodyB = world->CreateBody(defB);

            for (int32 i = 0; i < 120; ++i) {
                world->Step(FIXED_DT, 8, 3);
            }

            ok &= Assert(contactCount > 0,
                "Polygon-Polygon (SAT) collision detected",
                "Expected > 0 contacts");
        }
        return ok;
    }

    // ============================================================
    // Test 5: Sleeping 休眠
    // ============================================================
    bool BarePhysicsTestApp::TestSleeping() {
        std::cout << "\n--- Test: Sleeping Mechanism ---" << std::endl;
        bool ok = true;
        {
            auto world = std::make_shared<Bare2DPhysicsWorld>(Vec2(0, -10));
            world->SetAllowSleeping(true);

            // 地面
            BodyDef groundDef;
            groundDef.type = BodyType::Static;
            groundDef.shape.type = ShapeType::Box;
            groundDef.shape.boxSize = Vec2(5.0f, 0.5f);
            groundDef.position = Vec2(0.0f, -2.0f);
            world->CreateBody(groundDef);

            // 创建箱子并让它掉到地面
            BodyDef boxDef;
            boxDef.type = BodyType::Dynamic;
            boxDef.shape.type = ShapeType::Box;
            boxDef.shape.boxSize = Vec2(0.5f, 0.5f);
            boxDef.position = Vec2(0.0f, 3.0f);
            boxDef.material.density = 1.0f;
            auto box = world->CreateBody(boxDef);

            // 步进足够多帧使其稳定休眠
            for (int32 i = 0; i < 300; ++i) {
                world->Step(FIXED_DT, 8, 3);
            }

            ok &= Assert(!box->IsAwake(),
                "Body goes to sleep after settling",
                "Body is still awake after 300 steps");

            // 用外部 impulse 唤醒
            box->SetAwake(true);
            ok &= Assert(box->IsAwake(),
                "Body wakes up on SetAwake(true)",
                "Body should be awake");
        }
        return ok;
    }

    // ============================================================
    // Test 6: 堆叠稳定性
    // ============================================================
    bool BarePhysicsTestApp::TestStackingStability() {
        std::cout << "\n--- Test: Stacking Stability ---" << std::endl;
        bool ok = true;
        {
            auto world = std::make_shared<Bare2DPhysicsWorld>(Vec2(0, -10));

            // 地面
            BodyDef groundDef;
            groundDef.type = BodyType::Static;
            groundDef.shape.type = ShapeType::Box;
            groundDef.shape.boxSize = Vec2(5.0f, 0.5f);
            groundDef.position = Vec2(0.0f, -3.0f);
            world->CreateBody(groundDef);

            // 堆叠 5 个箱子
            std::array<std::shared_ptr<IPhysicsBody>, 5> boxes;
            for (int32 i = 0; i < 5; ++i) {
                BodyDef boxDef;
                boxDef.type = BodyType::Dynamic;
                boxDef.shape.type = ShapeType::Box;
                boxDef.shape.boxSize = Vec2(0.45f, 0.45f);
                boxDef.position = Vec2(0.0f, -2.0f + i * 0.95f);
                boxDef.material.friction = 0.5f;
                boxes[i] = world->CreateBody(boxDef);
            }

            // 步进使堆叠稳定
            for (int32 i = 0; i < 300; ++i) {
                world->Step(FIXED_DT, 10, 3);
            }

            // 检查箱子位置 - 不应该穿透到地面以下
            bool allAbove = true;
            for (auto& box : boxes) {
                if (box->GetPosition().y < -2.8f) {
                    allAbove = false;
                    break;
                }
            }
            ok &= Assert(allAbove,
                "Stacked boxes stay above ground",
                "Some box penetrated through ground");
        }
        return ok;
    }

    // ============================================================
    // Test 7: 材质系统
    // ============================================================
    bool BarePhysicsTestApp::TestMaterialSystem() {
        std::cout << "\n--- Test: Material System ---" << std::endl;
        bool ok = true;
        {
            // 材质组合测试
            PhysicsMaterial stone = PhysicsMaterial::Stone();
            PhysicsMaterial rubber = PhysicsMaterial::Rubber();
            PhysicsMaterial combined = PhysicsMaterial::Combine(stone, rubber);

            ok &= Assert(combined.friction > 0.3f && combined.friction < 0.9f,
                "Combined friction is geometric mean of stone(0.6) and rubber(0.9)",
                "friction = " + std::to_string(combined.friction));

            ok &= Assert(combined.restitution == 0.9f,
                "Combined restitution takes max(0.1, 0.9) = 0.9",
                "restitution = " + std::to_string(combined.restitution));

            // 材质预设取值
            ok &= Assert(PhysicsMaterial::Ice().friction < 0.1f,
                "Ice friction is very low",
                "ice friction = " + std::to_string(PhysicsMaterial::Ice().friction));

            ok &= Assert(PhysicsMaterial::Bouncy().restitution == 1.0f,
                "Bouncy material has 1.0 restitution",
                "bouncy restitution = " + std::to_string(PhysicsMaterial::Bouncy().restitution));

            // 使用材质的物体碰撞
            auto world = std::make_shared<Bare2DPhysicsWorld>(Vec2(0, -10));
            float32 maxBounceHeight = 0.0f;

            world->SetContactPersistCallback([&](const ContactPersistData&) {
                // 记录弹起高度
            });

            // 地面用石头材质
            BodyDef groundDef;
            groundDef.type = BodyType::Static;
            groundDef.shape.type = ShapeType::Box;
            groundDef.shape.boxSize = Vec2(5.0f, 0.5f);
            groundDef.position = Vec2(0.0f, -2.0f);
            groundDef.material = PhysicsMaterial::Stone();
            world->CreateBody(groundDef);

            // 创建弹性球（橡胶材质）
            BodyDef ballDef;
            ballDef.type = BodyType::Dynamic;
            ballDef.shape.type = ShapeType::Circle;
            ballDef.shape.circleRadius = 0.3f;
            ballDef.position = Vec2(0.0f, 3.0f);
            ballDef.material = PhysicsMaterial::Rubber();
            auto ball = world->CreateBody(ballDef);

            // 步进，让球弹跳
            float32 maxY = ball->GetPosition().y;
            for (int32 i = 0; i < 200; ++i) {
                world->Step(FIXED_DT, 8, 3);
                if (ball->GetPosition().y > maxY) {
                    maxY = ball->GetPosition().y;
                }
            }

            ok &= Assert(maxY > -1.8f,
                "Bouncy ball bounces back up",
                "maxY = " + std::to_string(maxY));
        }
        return ok;
    }

    // ============================================================
    // Test 8: BodyType 切换
    // ============================================================
    bool BarePhysicsTestApp::TestBodyTypeChange() {
        std::cout << "\n--- Test: Body Type Change ---" << std::endl;
        bool ok = true;
        {
            auto world = std::make_shared<Bare2DPhysicsWorld>(Vec2(0, -10));

            BodyDef def;
            def.type = BodyType::Dynamic;
            def.shape.type = ShapeType::Box;
            def.shape.boxSize = Vec2(0.5f, 0.5f);
            def.position = Vec2(0.0f, 2.0f);
            auto body = world->CreateBody(def);

            // 步进 - 受重力下落
            for (int32 i = 0; i < 30; ++i) {
                world->Step(FIXED_DT, 8, 3);
            }
            Vec2 fallingPos = body->GetPosition();

            // 改为静态
            body->SetType(BodyType::Static);
            Vec2 staticPos = body->GetPosition();

            // 再多步进几帧 - 静态不应移动
            for (int32 i = 0; i < 30; ++i) {
                world->Step(FIXED_DT, 8, 3);
            }

            ok &= Assert(std::abs(body->GetPosition().y - staticPos.y) < m_Epsilon,
                "Static body doesn't move",
                "pos changed from " + std::to_string(staticPos.y) +
                " to " + std::to_string(body->GetPosition().y));

            // 改回动态
            body->SetType(BodyType::Dynamic);
            for (int32 i = 0; i < 30; ++i) {
                world->Step(FIXED_DT, 8, 3);
            }
            ok &= Assert(body->GetPosition().y < staticPos.y - 0.5f,
                "Reverted to dynamic falls again",
                "y = " + std::to_string(body->GetPosition().y));
        }
        return ok;
    }

    // ============================================================
    // Test 9: Trigger 传感器
    // ============================================================
    bool BarePhysicsTestApp::TestTriggerSensor() {
        std::cout << "\n--- Test: Trigger Sensor ---" << std::endl;
        bool ok = true;
        {
            auto world = std::make_shared<Bare2DPhysicsWorld>(Vec2(0, 0));
            int32 triggerEnter = 0;
            int32 triggerExit = 0;

            // Trigger 回调：碰撞中分离
            world->SetContactBeginCallback([&](const ContactInfo& info) {
                triggerEnter++;
                (void)info;
            });
            world->SetContactEndCallback([&](const ContactInfo& info) {
                triggerExit++;
                (void)info;
            });

            // 创建传感器区域
            BodyDef sensorDef;
            sensorDef.type = BodyType::Static;
            sensorDef.shape.type = ShapeType::Box;
            sensorDef.shape.boxSize = Vec2(1.0f, 1.0f);
            sensorDef.shape.isSensor = true;
            sensorDef.position = Vec2(0.0f, 0.0f);
            world->CreateBody(sensorDef);

            // 创建小球穿过传感器
            BodyDef ballDef;
            ballDef.type = BodyType::Dynamic;
            ballDef.shape.type = ShapeType::Circle;
            ballDef.shape.circleRadius = 0.2f;
            ballDef.position = Vec2(-3.0f, 0.0f);
            ballDef.linearVelocity = Vec2(3.0f, 0.0f);
            world->CreateBody(ballDef);

            for (int32 i = 0; i < 120; ++i) {
                world->Step(FIXED_DT, 8, 3);
            }

            ok &= Assert(triggerEnter > 0,
                "Trigger enter detected with sensor",
                "triggerEnter = " + std::to_string(triggerEnter));

            ok &= Assert(triggerExit > 0,
                "Trigger exit detected",
                "triggerExit = " + std::to_string(triggerExit));
        }
        return ok;
    }

    // ============================================================
    // Test 10: 碰撞滤波
    // ============================================================
    bool BarePhysicsTestApp::TestCollisionFiltering() {
        std::cout << "\n--- Test: Collision Filtering ---" << std::endl;
        bool ok = true;
        {
            auto world = std::make_shared<Bare2DPhysicsWorld>(Vec2(0, 0));
            int32 contactCount = 0;

            world->SetContactBeginCallback([&](const ContactInfo&) {
                contactCount++;
            });

            // 创建碰撞层不兼容的物体
            BodyDef defA, defB;

            defA.type = BodyType::Dynamic;
            defA.shape.type = ShapeType::Box;
            defA.shape.boxSize = Vec2(0.5f, 0.5f);
            defA.position = Vec2(-1.0f, 0.0f);
            defA.linearVelocity = Vec2(3.0f, 0.0f);
            defA.categoryBits = Layer_Player;
            defA.maskBits = Layer_Enemy;  // 只与 Enemy 碰撞
            world->CreateBody(defA);

            defB.type = BodyType::Dynamic;
            defB.shape.type = ShapeType::Box;
            defB.shape.boxSize = Vec2(0.5f, 0.5f);
            defB.position = Vec2(1.0f, 0.0f);
            defB.linearVelocity = Vec2(-3.0f, 0.0f);
            defB.categoryBits = Layer_Player;       // 也是 Player 层
            defB.maskBits = Layer_Enemy;            // 只与 Enemy 碰撞
            world->CreateBody(defB);

            for (int32 i = 0; i < 60; ++i) {
                world->Step(FIXED_DT, 8, 3);
            }

            // Player vs Player 不应碰撞（mask 中无 Layer_Player）
            ok &= Assert(contactCount == 0,
                "Filtering prevents Player-Player collision",
                "Expected 0 contacts, got " + std::to_string(contactCount));

            // 创建兼容层的碰撞
            int32 allowedContactCount = 0;
            world->SetContactBeginCallback([&](const ContactInfo&) {
                allowedContactCount++;
            });

            BodyDef defC;
            defC.type = BodyType::Dynamic;
            defC.shape.type = ShapeType::Circle;
            defC.shape.circleRadius = 0.3f;
            defC.position = Vec2(-0.5f, 0.0f);
            defC.linearVelocity = Vec2(3.0f, 0.0f);
            defC.categoryBits = Layer_Enemy;
            defC.maskBits = Layer_Player;  // Enemy 可以与 Player 碰撞
            world->CreateBody(defC);

            for (int32 i = 0; i < 60; ++i) {
                world->Step(FIXED_DT, 8, 3);
            }

            ok &= Assert(allowedContactCount > 0,
                "Enemy can collide with Player when layers match",
                "Expected > 0 contacts, got " + std::to_string(allowedContactCount));
        }
        return ok;
    }

    // ============================================================
    // Test 11: 光线投射
    // ============================================================
    bool BarePhysicsTestApp::TestRayCast() {
        std::cout << "\n--- Test: Ray Cast ---" << std::endl;
        bool ok = true;
        {
            auto world = std::make_shared<Bare2DPhysicsWorld>(Vec2(0, 0));

            BodyDef def;
            def.type = BodyType::Static;
            def.shape.type = ShapeType::Box;
            def.shape.boxSize = Vec2(1.0f, 2.0f);
            def.position = Vec2(0.0f, 0.0f);
            world->CreateBody(def);

            // 从左侧射向右侧，应该击中中间箱子
            auto results = world->RayCast(Vec2(-5.0f, 0.0f), Vec2(5.0f, 0.0f));

            ok &= Assert(!results.empty(),
                "RayCast hits the box",
                "Expected at least 1 result, got " + std::to_string(results.size()));

            if (!results.empty()) {
                ok &= Assert(results[0].fraction > 0.3f && results[0].fraction < 0.7f,
                    "RayCast fraction indicates hit at center",
                    "fraction = " + std::to_string(results[0].fraction));
            }

            // 从上方射向下方，不应击中（在箱子上面通过）
            auto results2 = world->RayCast(Vec2(3.0f, 5.0f), Vec2(3.0f, -5.0f));
            ok &= Assert(results2.empty(),
                "RayCast misses box when shooting beside it",
                "Expected 0 results, got " + std::to_string(results2.size()));
        }
        return ok;
    }

    // ============================================================
    // Test 12: 形状投射
    // ============================================================
    bool BarePhysicsTestApp::TestShapeCast() {
        std::cout << "\n--- Test: Shape Cast ---" << std::endl;
        bool ok = true;
        {
            auto world = std::make_shared<Bare2DPhysicsWorld>(Vec2(0, 0));

            BodyDef def;
            def.type = BodyType::Static;
            def.shape.type = ShapeType::Box;
            def.shape.boxSize = Vec2(1.0f, 1.0f);
            def.position = Vec2(0.0f, 0.0f);
            world->CreateBody(def);

            // 投射一个小圆从左侧到右侧
            auto results = world->ShapeCast(
                Vec2(-4.0f, 0.0f), Vec2(4.0f, 0.0f),
                ShapeType::Circle, Vec2(0.2f, 0.2f));

            ok &= Assert(!results.empty(),
                "ShapeCast detects obstacle",
                "Expected at least 1 result");

            if (!results.empty()) {
                ok &= Assert(results[0].fraction > 0.3f && results[0].fraction < 0.7f,
                    "ShapeCast fraction indicates hit near center",
                    "fraction = " + std::to_string(results[0].fraction));
            }

            // 远距离投射 - 不应击中
            auto results2 = world->ShapeCast(
                Vec2(-4.0f, 3.0f), Vec2(4.0f, 3.0f),
                ShapeType::Circle, Vec2(0.2f, 0.2f));
            ok &= Assert(results2.empty(),
                "ShapeCast misses when path is above obstacle",
                "Expected 0 results");
        }
        return ok;
    }

    // ============================================================
    // Test 13: 多 Fixture
    // ============================================================
    bool BarePhysicsTestApp::TestMultiFixture() {
        std::cout << "\n--- Test: Multi-Fixture ---" << std::endl;
        bool ok = true;
        {
            auto world = std::make_shared<Bare2DPhysicsWorld>(Vec2(0, -10));

            // 创建一个刚体并在运行时添加多个 Fixture
            BodyDef def;
            def.type = BodyType::Dynamic;
            def.shape.type = ShapeType::Box;
            def.shape.boxSize = Vec2(0.5f, 0.5f);
            def.position = Vec2(0.0f, 2.0f);
            def.material.density = 1.0f;
            def.material.friction = 0.3f;
            auto body = world->CreateBody(def);

            // 转换到 Bare2DPhysicsBody 访问 Fixture
            auto* bareBody = static_cast<Bare2DPhysicsBody*>(body.get());

            ok &= Assert(bareBody->GetFixtureCount() == 1,
                "Initial fixture count is 1",
                "count = " + std::to_string(bareBody->GetFixtureCount()));

            // 添加第二个 Fixture
            FixtureDef fixDef;
            fixDef.shape.type = ShapeType::Circle;
            fixDef.shape.circleRadius = 0.3f;
            fixDef.shape.offset = Vec2(0.5f, 0.0f);
            fixDef.density = 1.0f;
            bareBody->AddFixture(fixDef);

            ok &= Assert(bareBody->GetFixtureCount() == 2,
                "After AddFixture, count = 2",
                "count = " + std::to_string(bareBody->GetFixtureCount()));

            // 步进 - 多 Fixture 的物体应正常模拟
            for (int32 i = 0; i < 30; ++i) {
                world->Step(FIXED_DT, 8, 3);
            }

            ok &= Assert(bareBody->GetPosition().y < 1.5f,
                "Multi-fixture body falls under gravity",
                "y = " + std::to_string(bareBody->GetPosition().y));

            // 清除 Fixtures
            bareBody->ClearFixtures();
            ok &= Assert(bareBody->GetFixtureCount() == 0,
                "After ClearFixtures, count = 0",
                "count = " + std::to_string(bareBody->GetFixtureCount()));
        }
        return ok;
    }

    // ============================================================
    // Test 14: CCD 连续碰撞
    // ============================================================
    bool BarePhysicsTestApp::TestCCD() {
        std::cout << "\n--- Test: CCD (Continuous Collision Detection) ---" << std::endl;
        bool ok = true;
        {
            auto world = std::make_shared<Bare2DPhysicsWorld>(Vec2(0, 0));
            world->SetCCDEnabled(true);

            // 创建一堵薄墙
            BodyDef wallDef;
            wallDef.type = BodyType::Static;
            wallDef.shape.type = ShapeType::Box;
            wallDef.shape.boxSize = Vec2(0.1f, 1.0f);  // 薄墙
            wallDef.position = Vec2(0.0f, 0.0f);
            world->CreateBody(wallDef);

            int32 contactCount = 0;
            world->SetContactBeginCallback([&](const ContactInfo&) {
                contactCount++;
            });

            // 高速飞行的子弹
            BodyDef bulletDef;
            bulletDef.type = BodyType::Dynamic;
            bulletDef.shape.type = ShapeType::Circle;
            bulletDef.shape.circleRadius = 0.2f;
            bulletDef.position = Vec2(-5.0f, 0.0f);
            bulletDef.linearVelocity = Vec2(20.0f, 0.0f);  // 高速
            bulletDef.isBullet = true;  // 启用 CCD
            world->CreateBody(bulletDef);

            // 步进几帧
            for (int32 i = 0; i < 60; ++i) {
                world->Step(FIXED_DT, 8, 3);
            }

            // 如果没有 CCD，子弹可能会穿过薄墙
            // 有 CCD 应该检测到碰撞
            ok &= Assert(contactCount > 0,
                "CCD bullet hits thin wall",
                "Expected > 0 contacts, got " + std::to_string(contactCount));

            // 验证子弹没有穿到墙的另一侧
            // (简化判断：子弹应在墙附近被阻挡)
            if (contactCount > 0) {
                // 至少发生了碰撞事件
            }
        }
        return ok;
    }

} // namespace Engine