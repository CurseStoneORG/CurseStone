// Human — base class for human entities (Players and NPC villagers).
// Holds inventory, armor, food, experience and the absorption buffer.
// Player subclass adds network session handling.
#pragma once
#include "Living.h"
#include <memory>
namespace cursestone::inventory { class PlayerInventory; }
namespace cursestone::entity {
class Human : public Living {
public:
    using Living::Living;
    void setInventory(std::shared_ptr<inventory::PlayerInventory> inv) { inv_ = std::move(inv); }
    std::shared_ptr<inventory::PlayerInventory> inventory() const { return inv_; }
    int foodLevel() const { return food_; }
    void setFoodLevel(int f) { food_ = f; }
    float saturation() const { return saturation_; }
    void setSaturation(float s) { saturation_ = s; }
    int experience() const { return xp_; }
    void setExperience(int x) { xp_ = x; }

    // ---- Absorcion (Genisys Human::getAbsorption/setAbsorption) ----
    // Genisys la guarda en el atributo ABSORPTION del AttributeMap; aca es
    // un entero directo, que es lo unico que el pipeline de dano consulta.
    // El escudo come el dano ANTES que los corazones (ver
    // Living::applyDamageEvent).
    int  absorptionPoints() const override { return absorption_; }
    void setAbsorptionPoints(int points) override { absorption_ = points < 0 ? 0 : points; }

protected:
    std::shared_ptr<inventory::PlayerInventory> inv_;
    int food_ = 20;
    float saturation_ = 5.0f;
    int xp_ = 0;
    int absorption_ = 0;
};
} // namespace
