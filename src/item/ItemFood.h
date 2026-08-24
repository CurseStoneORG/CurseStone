#pragma once
#include "Item.h"
namespace atomikpe::item {
class ItemFood : public Item {
public:
    ItemFood(uint16_t id, int food, float saturation = 0.6f)
        : Item(id), food_(food), saturation_(saturation) {}
    bool isFood() const override { return true; }
    int foodPoints() const override { return food_; }
    float saturation() const { return saturation_; }
private:
    int food_;
    float saturation_;
};
} // namespace
