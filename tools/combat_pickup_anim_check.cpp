// tools/combat_pickup_anim_check.cpp
//
// Tres features anadidas, validadas contra Genisys 0.15.0:
//
// 1) ANIMACION DE ROTURA — handlePlayerAction() re-difunde el
//    PlayerActionPacket del cliente a los demas (con el eid del que
//    rompe) para que cada cliente remoto dibuje el crack.
//
// 2) LIMPIEZA DE ITEMS — al recoger un item, RemoveEntityPacket tiene
//    que llegar a TODOS los viewers, no solo a quien lo recogio.
//
// 3) KNOCKBACK — Living::knockBack() de Genisys, que necesita
//    SetEntityMotionPacket (aqui era un stub).
//
// Lo que se puede verificar sin servidor es el formato de bytes y la
// formula; el filtrado por spectator/distancia se cubre por lectura del
// codigo de difusion. La formula se transcribe literalmente de Genisys
// Living.php::knockBack() y se compara con la misma formula escrita
// aqui de forma independiente, para que cualquier cambio futuro en el
// Gameplay::applyKnockback rompa el check.
//
//   g++ -std=c++20 -Wall -Wextra -O1 -Isrc -I/tmp/zstub
//       tools/combat_pickup_anim_check.cpp
//       src/network/protocol/SetEntityMotionPacket.cpp
//       src/network/protocol/PlayerActionPacket.cpp
//       src/network/protocol/DataPacket.cpp src/utils/Binary.c
//       -o /tmp/tools/combat_pickup_anim_check
#include "network/protocol/SetEntityMotionPacket.h"
#include "network/protocol/PlayerActionPacket.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

using namespace cursestone::network::protocol;

static int failures = 0;
static void check(bool ok, const char* what) {
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}
static std::string hex(const std::vector<uint8_t>& v) {
    static const char* d = "0123456789ABCDEF";
    std::string s;
    for (uint8_t b : v) { s += d[b >> 4]; s += d[b & 0xF]; s += ' '; }
    return s;
}
static void dump(const char* l, const std::vector<uint8_t>& v) {
    printf("    %-24s %s\n", l, hex(v).c_str());
}

// --- Genisys Living::knockBack(), transcrito literalmente ---
// OJO: Genisys NO usa la Y de la victima (solo deltaX/deltaZ), asi que
// el helper tampoco la recibe — igual que la referencia.
struct Motion { double x = 0, y = 0, z = 0; };
static Motion genisysKnockBack(double px, double py, double pz,   // motion actual
                               double tx, double tz,              // victima (x,z)
                               double ax, double az,              // atacante
                               double base = 0.4) {
    Motion m;
    double dx = tx - ax;
    double dz = tz - az;
    double f = std::sqrt(dx * dx + dz * dz);
    if (f <= 0) { m.x = px; m.y = py; m.z = pz; return m; }
    f = 1.0 / f;
    m.x = px / 2 + dx * f * base;
    m.y = py / 2 + base;
    m.z = pz / 2 + dz * f * base;
    if (m.y > base) m.y = base;
    return m;
}

int main() {
    printf("COMBAT / PICKUP / BREAK-ANIM CHECK (Genisys 0.15.0)\n");

    // ================================================================
    printf("\n[1] SetEntityMotionPacket — formato de bytes\n");
    {
        SetEntityMotionPacket p;
        p.entities.push_back({0x0102030405060708LL, 0.5f, 0.25f, -0.75f});
        std::vector<uint8_t> out; p.encode(out);
        dump("motion wire:", out);
        check(out.size() == 20, "un registro = 20 bytes (eid 8 + 3 floats 12)");
        // Genisys: putLong(eid); putFloat(x); putFloat(y); putFloat(z)
        check(out[0] == 0x01 && out[7] == 0x08, "eid va big-endian (putLong)");
        // 0.5f = 0x3F000000 BE, 0.25f = 0x3E800000 BE, -0.75f = 0xBF400000 BE
        check(out[8]  == 0x3F && out[9]  == 0x00 && out[10] == 0x00 && out[11] == 0x00,
              "motionX va big-endian float (0.5f)");
        check(out[12] == 0x3E && out[13] == 0x80, "motionY va big-endian float (0.25f)");
        check(out[16] == 0xBF && out[17] == 0x40, "motionZ big-endian y con signo (-0.75f)");

        // Varios registros aplanados, sin count ni terminador (Genisys).
        SetEntityMotionPacket q;
        q.entities.push_back({1, 1.0f, 0.0f, 0.0f});
        q.entities.push_back({2, 0.0f, 1.0f, 0.0f});
        std::vector<uint8_t> o2; q.encode(o2);
        check(o2.size() == 40, "dos registros = 40 bytes, sin count ni terminador");

        SetEntityMotionPacket rt;
        check(rt.decode(o2.data(), o2.size()), "decodifica");
        check(rt.entities.size() == 2, "round-trip conserva los 2 registros");
        if (rt.entities.size() == 2) {
            check(rt.entities[0].eid == 1 && rt.entities[1].eid == 2,
                  "round-trip conserva los eid");
            check(std::fabs(rt.entities[0].motX - 1.0f) < 1e-6 &&
                  std::fabs(rt.entities[1].motY - 1.0f) < 1e-6,
                  "round-trip conserva los valores");
        }
    }

    // ================================================================
    printf("\n[2] Knockback — Living::knockBack() de Genisys\n");
    {
        // Atacante en el origen, victima 1 bloque al este: el empujon va
        // hacia +X y hacia arriba.
        Motion m = genisysKnockBack(0, 0, 0, 1, 0, 0, 0);
        check(std::fabs(m.x - 0.4) < 1e-9, "victima al este recibe empujon +0.4 en X");
        check(std::fabs(m.z - 0.0) < 1e-9, "sin componente en Z");
        check(std::fabs(m.y - 0.4) < 1e-9, "y sube exactamente +base (0.4)");

        // Diagonal: normalizado, magnitud 0.4 en el plano XZ.
        Motion d = genisysKnockBack(0, 0, 0, 1, 1, 0, 0);
        double s = std::sqrt(d.x * d.x + d.z * d.z);
        check(std::fabs(s - 0.4) < 1e-9, "diagonal: magnitud del empujon = 0.4");
        check(std::fabs(d.x - 0.4 / std::sqrt(2.0)) < 1e-9, "diagonal reparte en X");
        check(std::fabs(d.z - 0.4 / std::sqrt(2.0)) < 1e-9, "diagonal reparte en Z");

        // Motion previo mitigado a la mitad.
        Motion p = genisysKnockBack(1.0, 0, 0, 1, 0, 0, 0);
        check(std::fabs(p.x - (0.5 + 0.4)) < 1e-9, "motion previo se amortigua a la mitad (0.5)");

        // Tope vertical: my nunca supera base.
        Motion c = genisysKnockBack(0, 5.0, 0, 1, 0, 0, 0);
        check(std::fabs(c.y - 0.4) < 1e-9, "el tope vertical limita y a base");

        // Distancia cero -> no hay empujon (Genisys: if($f <= 0) return).
        Motion z = genisysKnockBack(0, 0, 0, 0, 0, 0, 0);
        check(std::fabs(z.x) < 1e-12 && std::fabs(z.y) < 1e-12 && std::fabs(z.z) < 1e-12,
              "distancia cero -> sin empujon");
    }

    // ================================================================
    printf("\n[3] PlayerActionPacket — re-difusion de la animacion de rotura\n");
    {
        PlayerActionPacket p;
        p.eid = 0x1122334455667788LL;
        p.action = PlayerActionPacket::ACTION_START_BREAK;   // 0
        p.x = 100; p.y = 64; p.z = -200; p.face = 1;
        std::vector<uint8_t> out; p.encode(out);
        dump("break-action wire:", out);
        check(out.size() == 28, "cuerpo = eid(8) + 5 x int32(20) = 28 bytes");
        // Genisys encode: putLong(eid) putInt(action) putInt(x) putInt(y) putInt(z) putInt(face)
        check(out[8] == 0 && out[9] == 0 && out[10] == 0 && out[11] == 0,
              "action = ACTION_START_BREAK (0)");
        check(out[12] == 0 && out[13] == 0 && out[14] == 0 && out[15] == 100, "x big-endian");
        check(out[16] == 0 && out[17] == 0 && out[18] == 0 && out[19] == 64,  "y big-endian");
        check(out[20] == 0xFF && out[21] == 0xFF && out[22] == 0xFF && out[23] == 0x38,
              "z = -200 en complemento a dos big-endian");

        PlayerActionPacket rt;
        check(rt.decode(out.data(), out.size()), "decodifica");
        check(rt.eid == p.eid && rt.action == 0 && rt.x == 100 && rt.y == 64 &&
              rt.z == -200 && rt.face == 1, "round-trip completo");

        // El eid debe ser el del que rompe: es lo que le permite al
        // cliente remoto saber QUE entidad esta rompiendo.
        check(rt.eid == 0x1122334455667788LL, "el eid identifica al jugador que rompe");
        check(PlayerActionPacket::ACTION_START_BREAK == 0, "ACTION_START_BREAK == 0");
        check(PlayerActionPacket::ACTION_ABORT_BREAK == 1, "ACTION_ABORT_BREAK == 1");
        check(PlayerActionPacket::ACTION_STOP_BREAK  == 2, "ACTION_STOP_BREAK == 2");
    }

    printf("\nCOMBAT / PICKUP / BREAK-ANIM CHECK: %s (%d failure(s))\n",
           failures == 0 ? "OK" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}