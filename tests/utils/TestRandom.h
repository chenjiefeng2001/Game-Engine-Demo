/**
 * @file TestRandom.h
 * @brief 测试用固定种子随机数工具
 *
 * 使用固定种子 42，确保每次测试结果完全一致，
 * 消除"偶发失败"（Flaky Tests）。
 */
#pragma once
#include <random>

class TestRandom {
public:
    static std::mt19937& GetEngine() {
        static std::mt19937 engine(42);
        return engine;
    }

    static void Reset(unsigned seed = 42) {
        GetEngine().seed(seed);
    }

    static float Float(float min = 0.0f, float max = 1.0f) {
        std::uniform_real_distribution<float> dist(min, max);
        return dist(GetEngine());
    }

    static int Int(int min, int max) {
        std::uniform_int_distribution<int> dist(min, max);
        return dist(GetEngine());
    }
};