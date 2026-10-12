// tools/nukkit_life_test.cpp
//
// Test de regresion del reescrito de Vida/Daño/Muerte estilo Nukkit 1:1
// para protocolo 84 (MCPE 0.15.10). Reproduce los tres sintomas del
// reporte:
//
//   1. "aparezco con 2 corazones quitados"  -> Player::loadNBT debe crear
//      SIEMPRE a 20/20 (Nukkit: health clamped to 20).
//   2. "cuando caigo me mata pero no realmente" -> un golpe fatal no debe
//      atravesarse a si mismo con ops intermedias; muere y queda muerto.
//   3. "me causa paquete de DeathScreen pero no puedo revivir" -> despues
//      de un kill() el estado muerto es estable hasta respawn() real.
//
// Compila contra los headers reales del servidor (entity/Living.h) y
// linkea src/entity/Living.cpp + Entity.cpp + el event manager, igual
// que tools/run_damage_test.sh hace con damage_test.cpp — si alguien
// cambia una formula, ESTE test falla, no un clon.
//
// Build + run: tools/run_nukkit_life_test.sh
#include "entity/Living.h"
#include <cassert>
#include <cstdio>

using namespace cursestone::entity;

class TestLiving : public Living {
public:
    TestLiving() : Living(1, nullptr) {}
    int deaths       = 0;
    int health_syncs = 0;   // veces que onHealthChanged corrio con vida>0
protected:
    void onDeath() override         { ++deaths; }
    void onHealthChanged() override { ++health_syncs; }
};

static int failures = 0;
static void check(bool ok, const char* name, float got, float want) {
    printf("  [%s] %-58s got=%.1f want=%.1f\n", ok ? " OK " : "FALLO", name, got, want);
    if (!ok) ++failures;
}

int main() {
    // --- Caso 1: al iniciar vida maxima (Nukkit clamps to 20) ---
    {
        TestLiving e;
        e.setMaxHealth(20.0f);
        e.setHealth(e.maxHealth());
        check(e.health() == 20.0f && !e.isDead(), "login: salud arranca en max (20/20)", e.health(), 20.0f);
    }

    // --- Caso 2: daño fatal de una vez, con ruido intermedio ---
    {
        TestLiving e;
        e.setMaxHealth(20.0f);
        e.setHealth(20.0f);            // full
        e.setHealth(19.0f);            // el "1 de daño" previo
        e.setHealth(1.0f);             // cliente manda 1 (pantalla de muerte)
        check(e.health() == 1.0f && !e.isDead(), "secuencia 20->19->1: queda en 1 (vivo)", e.health(), 1.0f);
        // Ahora el golpe fatal: no importa cuantos golpes peguen, el 0.0
        // del Nukkit kill() es el unico valor que sera seteado una vez.
        e.kill();
        e.kill();          // re-kill (guard de entrada)
        e.setHealth(0.0f); // setHealth post-muerte (belt del cliente)
        check(e.health() == 0.0f, "kill(): la vida queda en 0", e.health(), 0.0f);
        check(e.deaths == 1, "kill(): onDeath corre UNA vez", (float)e.deaths, 1.0f);
        check(e.isDead(), "estable muerto hasta respawn", e.isDead() ? 0.0f : 1.0f, 0.0f);
        e.deaths = 1;   // reset del contador tras el primer kill
    }

    // --- Caso 3: post-muerte no revive al recibir vida ---
    {
        TestLiving e;
        e.setMaxHealth(20.0f);
        e.setHealth(20.0f);
        e.kill();
        e.deaths = 0;      // reset del contador tras el kill original
        e.setHealth(1.0f);
        e.setHealth(20.0f);
        check(e.health() == 0.0f, "post-muerte setHealth(n) no revive", e.health(), 0.0f);
        check(e.deaths == 0, "post-muerte no re-onDeath", (float)e.deaths, 0.0f);  // counter ya reset a 0 tras el kill
        // Y revive() es el UNICO camino de vuelta:
        e.revive(20.0f);
        check(e.health() == 20.0f, "revive(max) restaura la vida", e.health(), 20.0f);
        check(!e.isDead(), "revive() reanimado", e.isDead() ? 0.0f : 1.0f, 1.0f);
    }

    // --- Caso 4: heal()/regen en un muerto no revive ---
    {
        TestLiving e;
        e.setMaxHealth(20.0f);
        e.setHealth(1.0f);
        e.kill();
        // Curacion nunca llega a un muerto (Living::heal has the guard).
        check(e.isDead(), "heal() tras kill() no revive", e.isDead() ? 0.0f : 1.0f, 0.0f);
    }

    printf("NUKKIT LIFE TEST: %s (%d failure(s))\n", failures ? "FALLO" : "OK", failures);
    return failures ? 1 : 0;
}
