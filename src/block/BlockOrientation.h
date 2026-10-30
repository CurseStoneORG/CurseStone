/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
| \__/\ |_| | |  \__ \  __/\__/ / || (_) | | | |  __/
 \____/\__,_|_|  |___/\___\____/ \__\___/|_| |_|\___|

 * CurseStone | Open Source Server Software
 *
 * Copyright (C) 2026 CurseStoneTeam
 *
 * Developed by CurseStoneTeam.
 * Built for an open and extensible Minecraft server ecosystem.
 *
 * Licensed under the GNU Lesser General Public License v3.0 or later.
 * You may study, modify, and redistribute this software under its license.
 *
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 * Repository: https://github.com/CurseStoneTeam/CurseStone
 *
 */
// SPDX-License-Identifier: LGPL-3.0-or-later
// BlockOrientation.h — vanilla block orientation tables for MCPE 0.15
// (protocol 84) legacy 4-bit block metadata.
//
// Todas las tablas provienen DIRECTAMENTE de las clases equivalentes de
// Nukkit (cn.nukkit.block.*) que es la referencia usada por el resto del
// proyecto para el comportamiento vanilla de este protocolo:
//
//   Escaleras        BlockStairs.place      faces {2,1,3,0} sobre el cuadrante E,W,S,N + bit 0x04 invertida
//   Puerta           BlockDoor.place        faces {1,2,3,0} + bit bisagra 0x01 en la mitad superior
//   Piston           (project's earlier scheme) {3,4,2,5}
//   Horno/Cofre      BlockFurnaceBurning / BlockChest  faces {2,5,3,4}
//   Dispensador      BlockDispenser         cara opuesta a la de click (index 0..5)
//   Hopper           BlockHopper            cara opuesta; UP colapsa a DOWN (0)
//   Logs/Quartz      BlockWood / BlockQuartz  axis: Y=0/4, Z=8, X=4 per clicked face
//   Calabaza         BlockPumpkin           cuadrante OPUESTO al del jugador (dan asco)
//   Repeater         BlockRedstoneDiode     cuadrante OPUESTO al del jugador
//   Trampilla        BlockTrapdoor.place    faces {2,1,3,0} + bit 0x04 (arriba)
//   Escalera de mano BlockLadder.place      metadata = cara clickeada (2..5)
//   Antorcha         BlockTorch.place       top→5; N:4, S:3, W:2, E:1
//   Button           BlockButton.place      metadata = clicked face (2..5)
//   Palanca          BlockLever (LeverOrientation.forFacings)
//   Cartel de pared  BlockSignPost.place    metadata = cara clickeada (2..5)
//   Vid              BlockVine.place        bit por cara: N=1, S=4, E=8, W=2
//   Yunque           BlockAnvil.place       faces {1,2,3,0} (eje este-oeste por defecto)
//
// The quadrant index uses the same yaw→quadrant formula as
// ya aplicaba el proyecto para puertas y pistones, asi ambos sistemas
// quedan alineados y los mapeos son consistentes entre si.
#pragma once
#include <cstdint>

namespace cursestone::block {

struct BlockOrientation {
    // yaw (grados, [0,360)) → cuadrante 0=sur,1=oeste,2=norte,3=este.
    static uint8_t quadrant(float yawDeg) {
        yawDeg = yawDeg - 360.0f * (int)(yawDeg / 360.0f);
        if (yawDeg < 0.0f)
            yawDeg += 360.0f;
        return (uint8_t)(((int)((yawDeg + 45.0f) / 90.0f)) & 0x03);
    }

    // Cuadrante del jugador (hacia donde mira).
    static uint8_t yawToMeta(float yaw) {
        return quadrant(yaw);
    }

    // Cuadrante OPUESTO (calabaza, repeater: miran hacia el jugador).
    static uint8_t yawToMetaOpposite(float yaw) {
        return (uint8_t)((quadrant(yaw) + 2) & 0x03);
    }

    // Cara clickeada (0..5) → metadata "de cara" para bloques pegados a
    // paredes: 2=norte, 3=sur, 4=oeste, 5=este. Cara arriba/abajo → 0
    // (although in practice the caller already filters those faces).
    static uint8_t faceIndexToMeta(uint8_t face) {
        static const uint8_t m[6] = {0, 0, 2, 3, 4, 5};
        return (face < 6) ? m[face] : 0;
    }

    // Antorcha: cara de click → metadata vanilla.
    // top(no aplica)/up→5 (antorchas de suelo), N→4, S→3, W→2, E→1.
    static uint8_t torchMeta(uint8_t face) {
        static const uint8_t m[6] = {5, 5, 4, 3, 2, 1};
        return (face < 6) ? m[face] : 5;
    }

    // Vine: clicked face → orientation bit. N→0x01, S→0x04, E→0x08, W→0x02.
    static uint8_t vineMeta(uint8_t face) {
        static const uint8_t m[6] = {0, 0, 1, 4, 2, 8};
        return (face < 6) ? m[face] : 0;
    }

    // Ladder/button/wall sign: metadata = clicked face as-is.
    static uint8_t ladderMeta(uint8_t face) {
        return (face >= 2 && face <= 5) ? face : 2;
    }

    // Logs / quartz / hay: axis per clicked face.
    // Y (up/down face) = no rotation, Z (N/S faces) = 0x08, X (E/W) = 0x04.
    // `meta` conserva los bits bajos (tipo de madera/variante).
    static uint8_t axisMeta(uint8_t face, uint8_t meta) {
        static const uint8_t bits[6] = {0, 0, 0x08, 0x08, 0x04, 0x04};
        return (uint8_t)((meta & 0x03) | ((face < 6) ? bits[face] : 0));
    }

    // Stairs: player quadrant → 2 orientation bits.
    static uint8_t stairsMeta(uint8_t quadrantBits) {
        static const uint8_t m[4] = {2, 1, 3, 0}; // E,W,S,N
        return m[quadrantBits & 0x03];
    }

    // Horno / cofre / Dispensador horizontal: cuadrante → "cara" 0..5.
    static uint8_t horizontalFaceMeta(uint8_t quadrantBits) {
        static const uint8_t m[4] = {2, 5, 3, 4};
        return m[quadrantBits & 0x03];
    }

    // Door: quadrant → orientation {1,2,3,0} (Nukkit BlockDoor).
    static uint8_t doorMeta(uint8_t quadrantBits) {
        static const uint8_t m[4] = {1, 2, 3, 0};
        return m[quadrantBits & 0x03];
    }

    // Piston: quadrant → facing {3,4,2,5} (the project's earlier scheme).
    static uint8_t pistonMeta(uint8_t quadrantBits) {
        static const uint8_t m[4] = {3, 4, 2, 5};
        return m[quadrantBits & 0x03];
    }

    // Yunque: cuadrante → {1,2,3,0}.
    static uint8_t anvilMeta(uint8_t quadrantBits) {
        static const uint8_t m[4] = {1, 2, 3, 0};
        return m[quadrantBits & 0x03];
    }

    // Trampilla: cuadrante → {2,1,3,0} (Nukkit BlockTrapdoor).
    static uint8_t trapdoorMeta(uint8_t quadrantBits) {
        static const uint8_t m[4] = {2, 1, 3, 0};
        return m[quadrantBits & 0x03];
    }

    // Palanca: cara de click + yaw del jugador → metadata LeverOrientation.
    // Tabla vanilla de LeverOrientation.forFacings(clickFace, playerFacing):
    //   click abajo (0)  → 0 (down_x, eje X)
    //   click arriba (1) → 5 (up_x, eje X)   [click cara UP monta sobre el bloque]
    //   laterales: montada sobre la cara opuesta al click;
    //   N→3, S→2, E→1, W→4, y la mitad superior del bloque (fy>0.5) en
    //   caras N/S rota a 6/7.
    static uint8_t leverMeta(uint8_t face, bool topHalf) {
        static const uint8_t side[6] = {0, 5, 3, 2, 1, 4};
        uint8_t m = side[(face < 6) ? face : 0];
        if (face == 1)
            return 5;                            // click en cara superior
        if ((face == 2 || face == 3) && topHalf) // mitad superior, caras N/S
            m = (m == 3) ? 6 : 7;
        return m;
    }
};

} // namespace cursestone::block
