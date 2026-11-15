# CURSESTONE — Investigación del posible Use-After-Free en RakLib

**Fecha:** 2026-11-01
**Objeto de estudio:** señal de la auditoría `CURSESTONE_ARCHITECTURE_AUDIT.md` §12.1 — UAF en el índice de sesiones tras kick.
**Método:** análisis estático de solo lectura en cuatro vías paralelas (ciclo de vida, memoria/punteros, concurrencia/red, verificación independiente), coordinadas y contrastadas por un revisor central que re-verificó en el árbol cada afirmación incorporada. **No se modificó, creó ni refactorizó ningún archivo del repositorio** (la única medición dinámica fue una sonda de compilación en `/tmp` para obtener `sizeof`, sin tocar el repo).
**Regla aplicada en todo el informe:** un puntero raw no es automáticamente un UAF; cada veredicto va acompañado de archivo, función, líneas, fragmento, secuencia de ejecución y nivel de certeza (`confirmado` / `probable` / `descartado`). Lo no verificable se declara en §10.

---

## 1. Resumen ejecutivo

**VEREDICTO: el Use-After-Free está CONFIRMADO como defecto de código, con las tres rutas de explotación demostradas a nivel de flujo de ejecución.**

- `mcp_server_handler_close_session` (`src/network/raklib/ServerHandler.cpp:72-87`) libera la sesión y la saca del array `sessions[]`, pero **nunca borra la entrada del índice O(1)** `map_key/map_ptr` (`SessionManager.h:74-75`). El único `sm_map_erase` de todo el árbol vive en el sweep de timeout (`SessionManager.cpp:2386`), que itera el **array** — la sesión liberada ya no está en él, así que la entrada huérfana **nunca se purga** hasta el apagado del proceso.
- El UAF **no requiere condición de carrera alguna**: creación, despacho, cierre y sweep corren todos en el hilo de RakLib. Basta un kick (o una IP bloqueada, o un batch malformado) y **cualquier datagrama posterior del mismo endpoint** — p. ej. el ACK que el propio cliente envía del paquete de desconexión — para que `find` devuelva el puntero liberado.
- Tres escenarios demostrados: **S1** kick → ACK tardío → UAF en `handle_ack` (confirmado; comportamiento de cliente trivial); **S2** reconexión desde la misma IP:puerto → el handshake completo corre sobre memoria liberada y el servidor "conecta" a un jugador zombi (ruta confirmada); **S3** batch malformado → ventana de envío posterior al cierre (ruta confirmada, ataque probable).
- Riesgos derivados: **envenenamiento permanente del endpoint** (ninguna reconexión desde esa ip:puerto vuelve a crear una sesión real), **aliasing cruzado entre sesiones** si el allocator reutiliza el bloque de ~350 KB (probable), **free de puntero basura** (probable) y **congelación permanente del hilo de red por mapa lleno** con ~1024 endpoints kickeados sin reconexión (comportamiento confirmado en el código; alcanzable como DoS por la ruta bad-batch). El **double-free del struct está descartado**.
- Manifestación en runtime (análisis, no medido en servidor vivo): `sizeof(mcp_rak_session_t) = 349.848 bytes` (medido por compilación) supera el umbral mmap inicial de glibc (128 KB) → el primer ciclo free→uso puede ser **SIGSEGV inmediato** (munmap); tras la subida del threshold dinámico de glibc, las siguientes sesiones salen del heap y el mismo defecto se manifiesta como **corrupción silenciosa** — el peor de los dos mundos.
- **Causa raíz:** dos registros del mismo conjunto (array propietario + índice no propietario) con un contrato manual —"mapa y array deben permanecer coherentes"— implementado en dos sitios distintos; `close_session` ejecutó una mitad. Es una regresión de la migración al índice O(1) (`11bd1d3`), no del diseño original del sweep.
- **Recomendación:** hotfix consolidando el teardown en una única función del `SessionManager` (con localización por array, no por mapa — ver §7) y, a medio plazo, dueño único RAII (`unordered_map<uint64_t, unique_ptr<Session>>`) que elimina estructuralmente la clase del error. El fix literal "una línea" de la auditoría **no es posible tal como está redactado** (`sm_map_erase` es `static`), y una variante ingenua que localice por mapa introduciría un segundo bug tras el aliasing.

---

## 2. Arquitectura del ciclo de vida de las sesiones

### 2.1 Componentes y propiedad

| Componente | Dónde vive | Propiedad | Papel |
|---|---|---|---|
| `sessions[]` + `session_count/capacity` | `SessionManager.h:52-54` | **El manager (dueño)** | Registro propietario; asigna (`get_or_create:191`) y libera (`:77`, `:2388`, `ServerHandler.cpp:82`) |
| `map_key[]` / `map_ptr[]` (1024 slots) | `SessionManager.h:74-75` | **Nadie (índice no propietario)** | `endpoint → sesión*` O(1); se inserta solo en `get_or_create:192`, se borra solo en el sweep `:2386` |
| `mcp_server_handler_t.manager` | `ServerHandler.h` | préstamo | Puente para acciones del servidor; `close_session` es su única operación viva |
| Colas `outbound_queue_` / `inbound_queue_` | `RakLibInterface.h:76-90, 64-71` | el puente C++ | Transportan **strings ip:port + bytes**, nunca punteros a sesión (verificado) |
| Registro del hilo principal | `Network.h:106` (`unordered_map<string, weak_ptr<Player>>`) | Network | Fuera del alcance raklib; nunca retiene `mcp_rak_session_t*` (verificado con grep sobre `src/` completo) |

### 2.2 Diagrama del ciclo (todo en el hilo de RakLib)

```
        OCR1/OCR2 de endpoint nuevo (SessionManager.cpp:2038 / :2081)
                          │ get_or_create :171-194
                          │ find:173 → calloc:187 → init:190 → array:191 → sm_map_insert:192
                          ▼
   UNCONNECTED → CONNECTING_1 (:2057) → CONNECTING_2 (:2119) → CONNECTED (:1350)
                          │  tráfico establecido: find :2239/:2245/:2254 → handle_*
        ┌─────────────────┼──────────────────────────┬─────────────────────────┐
        │ (A) 0x15 CLIENT_DISCONNECT          (B) timeout 5 s   │ (C) cierre servidor
        │  :1361-1371/:1421-1432              is_timed_out      │  kick/ban/bad-batch →
        │  on_close_session + is_disconnecting│                  │  cola outbound → drain
        │  NO libera; queda para el sweep     │                  │  → close_session
        ▼                                     ▼                  ▼
   SWEEP (:2267-2389): should_remove (:2323-2324)
   → sm_map_erase :2386 → destroy :2387 → free :2388 → swap-remove :2389   [COHERENTE]
   CLOSE_SESSION (ServerHandler.cpp:72-87):
   → destroy :81 → free :82 → swap-remove :83                              [❌ SIN erase]
   (D) apagado: join → manager_destroy (:70-82): libera array + memset total [COHERENTE]
```

### 2.3 Invariantes del doble registro

| Invariante | Estado | Evidencia |
|---|---|---|
| INV-1: toda sesión del array tiene entrada de mapa | ✔ preservado | insert incondicional en creación (`:191-192`); erase emparejado en sweep (`:2386`) |
| INV-2: toda entrada del mapa apunta a una sesión del array | **❌ roto solo por `close_session`** | `ServerHandler.cpp:81-84` sin erase |
| INV-3: sin claves duplicadas | ✔ | `get_or_create` retorna antes de insertar si `find` acierta (`:173-175`) |
| INV-4: capacidad mapa ≥ 2× sesiones | ✔ (1024/512) | `SessionManager.h:43,46-47` |

### 2.4 Matriz hilo → funciones que tocan estado de sesiones

| Hilo | Acceso | Protección |
|---|---|---|
| **RakLib** (`raklibThreadLoop`, `RakLibInterface.cpp:142-285`) | drenaje outbound (`:186-244`): `send_encapsulated` (`:233`), `close_session` (`:236-238`); `mcp_raklib_server_tick` (`:259`): recv, dispatch (`find` `:2239/:2245/:2254`, OCR `:2014/:2067`), sweep (`:2267-2389`) | **Ninguna (monopolio del hilo)** |
| Principal | solo encola (`sendRawPacket :355-373`, `closeSession :378-395`, `setServerName :342-350`) y drena inbound (`drainEvents :295-337`) | `outbound_mtx_` / `inbound_mtx_` / `motd_mtx_` |
| Principal (apagado) | `shutdown()` `:122-137`: stop_flag → join → `mcp_raklib_server_stop` | happens-before del join |
| — | `mcp_server_handler_send_encapsulated` y `send_raw` (`ServerHandler.cpp:40-70`): **cero llamadores en `src/`** — código muerto | — |

**Conclusión de concurrencia:** ningún hilo fuera de RakLib toca `sessions[]`, el mapa ni el struct de sesión; los kicks desde consola/RCON/Lua terminan todos en `scheduleSync` al hilo principal y de ahí a la cola. El UAF es **monohilo y lógico**, no una carrera.

---

## 3. Resultados individuales de los agentes

### 3.1 Agente 1 — Ciclo de vida

- **Único punto de creación**: `get_or_create` (`calloc :187` + `init :190` + array `:191` + insert `:192`); los únicos llamadores son los handlers OCR1 (`:2038`) y OCR2 (`:2081`). Verificado por grep: no hay otro `calloc`/`init` de sesiones en `src/network/raklib/`.
- **Tabla de rutas de destrucción** (reproducida en §2.2): sweep **coherente**; `close_session` **incoherente** (sin erase); 0x15 no destruye (marca `is_disconnecting` y espera al sweep, que sí erasea); apagado coherente por `memset` total (`:81`); doble close = no-op seguro (escanea solo el array).
- **H2 (confirmado)**: la reconexión recibe el colgante y la entrada huérfana es **auto-perpetua** — como `find` nunca devuelve NULL para esa clave, `sm_map_insert` no vuelve a ejecutarse jamás para ese endpoint; el sweep no la ve (no está en el array). Endpoint envenenado hasta el apagado.
- **H3 (confirmado, complementa §12.1)**: la ruta de ENVÍO del drenaje (`RakLibInterface.cpp:233`) también resuelve por mapa (`SessionManager.cpp:272`); la variante segura que escanea el array (`ServerHandler.cpp:55-61`) es **código muerto sin llamadores**.
- **H5 (confirmado, benigno en memoria)**: las dos ramas de 0x15 (CONNECTING_2 `:1361-1371` y CONNECTED `:1421-1432`) solo marcan `is_disconnecting`; el sweep las reaps con erase en el siguiente tick (~10 ms). Matiz funcional no-UAF: un reconectado en esa ventana pierde su handshake.
- **Cadena completa de cierres de servidor verificada**: KickCommand.cpp:44 y BindingsCore.cpp:204 (Lua) → `Player::kick` (`Player.cpp:320-341`); excepciones de handler (`PlayerNetwork.cpp:707`); login rechazado (`PlayerLogin.cpp:97-205`); IP bloqueada (`Network.cpp:73-76`); batch malformado (`Network.cpp:116-139`). Todos terminan encolando `CLOSE_SESSION`.

### 3.2 Agente 2 — Memoria y punteros

- **Inventario de punteros**: `sessions[i]` (dueño: manager), `map_ptr[i]` (no propietario — puede colgar), préstamos intra-llamada en dispatch (`:272, :2014, :2067, :2239, :2245, :2254`), y los punteros internos del struct (`recovery[].data`, `backlog[].data`, `order_hold[][].data`, `split_slots[].segments[]`, dueño: la sesión, liberados por `destroy`). **Ninguna estructura fuera de raklib retiene un `mcp_rak_session_t*`**; las colas del puente solo llevan strings/bytes.
- **Permanencia del huérfano, razonada sobre el algoritmo exacto**:
  - (a) `sm_map_insert` (`:119-126`) solo escribe slots con `map_ptr == NULL` → el huérfano (no-NULL) nunca se sobrescribe; además `find` resuelve antes de llegar a insertar.
  - (b) El back-shift de `sm_map_erase` (`:129-156`) **copia pares (key,ptr) intactos** al rellenar huecos: puede *reubicar* el huérfano, nunca limpiarlo (el único slot que se pone a NULL es el de la entrada buscada, `:154-155`).
  - (c) El sweep y `manager_destroy` iteran el **array** (`:2267-2268`, `:74-78`); el huérfano no está en él.
  - **Conclusión: entrada colgante permanente** durante la vida del proceso.
- **Accesos post-free enumerados** (tabla §4, E-7): lecturas de guardias que ya son UB (`s->state` en `:273`, `:2015`, `:2104`, `:1279`), escrituras incondicionales con datos parcialmente controlados por el cliente (`mtu`, `client_id` — 8 bytes —, `state`, timestamps) y los puntos más peligrosos: `free(s->recovery[slot].data)` en `:1923`, `free(data)` del backlog en `:262`, y **envío UDP de memoria liberada** en `handle_nack` (`:1963-1964`).
- **Double-free del struct: descartado** — las 3 liberaciones operan solo sobre miembros del array y `destroy` termina en `memset(s,0,sizeof(*s))` (`Session.cpp:128`). Double-free de **buffers internos vía aliasing**: probable (§5, riesgo D).

### 3.3 Agente 3 — Networking y concurrencia

- **Matriz de hilos** (§2.4): aislamiento real; **ninguna ruta desde otro hilo toca `raklib_`/`handler_` directamente**; la publicación de `raklib_` al hilo nuevo es segura (todo el init ocurre antes del spawn, `RakLibInterface.cpp:94-109`).
- **Carreras candidatas**:
  - (a) SEND tras CLOSE en el **kick**: **descartada** — FIFO garantiza `[SEND(disc), CLOSE]`; tras `close()`, `connected_=false` y `pending_removal_=true` cierran las gates de envío (`PlayerNetwork.cpp:96,118,144`; broadcasts excluyen `isPendingRemoval()`, `Server.cpp:594`). La variante sweep tiene ventana de envío pero es segura porque el sweep sí erasea el mapa.
  - (b) Callbacks/estado compartido: descartada (todo bajo mutexes; atómicos para flags).
  - (c) SESSION_CLOSE → main: sin bucle de re-close (`Player::close` no llama `closeSession`, `Player.cpp:310`); doble close = no-op.
  - (d) SHUTDOWN: acciones pendientes se abandonan sin efecto; `closeSession` post-stop es no-op por `running_==false`.
  - (e) Reconexión con sesión colgante: **confirmada como UAF sin carrera**.
- **Línea temporal exacta del kick** (t0–t7) incorporada en §5 (S1).
- Hallazgo funcional (no memoria): el kick-path **nunca emite SESSION_CLOSE** hacia el principal → la entrada `Network::sessions_["ip:port"]` queda huérfana (weak_ptr expira) — corrupción de estado, no de memoria.

### 3.4 Agente 4 — Verificación independiente

- **Veredicto: UAF confirmado**; refutación activa sin resultado (§4, E-10): entre el `free` y cada uso **no existe ninguna mitigación** — los 7 consumidores resuelven por mapa, no hay revalidación por array en rutas vivas (la única función que escanea el array está muerta), no hay campo de generación/época, y las guardias de estado **leen memoria liberada para decidir** (UB intrínseco aunque luego rechacen).
- **Correcciones que introdujo el verificador** (adoptadas por el coordinador tras re-verificación):
  1. El hang por mapa lleno se manifiesta en **`find`** (`:163-167`, bucle sin NULL que cortar cuando la tabla está llena y la clave no está), no en `insert` — `get_or_create` llama a `find` primero, que cuelga antes.
  2. La localización del teardown debe hacerse por **array**, no por mapa: si el bloque del huérfano fue reutilizado por la sesión viva de otro jugador (aliasing), un `close(key_A)` guiado por el mapa destruiría la sesión viva de B.
  3. El fix "una línea" de la auditoría no es literalmente posible: `sm_map_erase`/`sm_ep_key` son `static` en `SessionManager.cpp` (`:109-156`) y no accesibles desde `ServerHandler.cpp`.
  4. En el primer ciclo post-free sin reutilización, el memset de `destroy` deja el bloque a cero → el `free(s->recovery[slot].data)` de `:1923` no ejecuta (NULL) → el UAF inmediato es de metadatos (sigue siendo UB pleno; ASan lo reporta; con mmap→SIGSEGV).
- **Contradicción resuelta por el coordinador (Agente 2 vs Agente 3):** el Agente 3 había razonado que el byte `state` "suele retener 3 (CONNECTED)" porque tcache solo sobrescribe los primeros 16 bytes. **Es incorrecto**: `mcp_rak_session_destroy` termina en `memset(s,0,sizeof(*s))` (`Session.cpp:128`), verificado por lectura directa. El contenido post-free es ceros + metadatos del allocator; las decisiones de las guardias dependen del reuso posterior del bloque, pero **ninguna combinación evita el acceso a memoria liberada**. El veredicto no cambia.
- **Contradicción resuelta (Agente 1 vs Agente 3 sobre la ventana SEND-después-de-CLOSE):** ambos tenían razón a medias. Para el **kick** está descartada (gates de `close()`); pero el Agente 3 no analizó la ruta **bad-batch** (`Network.cpp:138`), que encola el cierre **sin cerrar el Player**: el re-análisis del Agente 4 (S3, §5) la confirma como ventana real.

---

## 4. Evidencias verificadas en el código

**E-1 · El defecto puntual — `close_session` sin erase** `confirmado`
`src/network/raklib/ServerHandler.cpp` — `mcp_server_handler_close_session` — líneas 72-87:

```c
void mcp_server_handler_close_session(mcp_server_handler_t* h, const uint8_t ip[4], uint16_t port) {
    if (!h || !h->manager)
        return;
    mcp_rak_session_manager_t* m = h->manager;
    for (size_t i = 0; i < m->session_count; ++i) {
        if (memcmp(m->sessions[i]->ip, ip, 4) == 0 && m->sessions[i]->port == port) {
            mcp_rak_session_destroy(m->sessions[i]);
            free(m->sessions[i]);
            m->sessions[i] = m->sessions[--m->session_count];
            return;
        }
    }
}
```

Entre `:81` y `:84` no hay ninguna operación sobre `map_key/map_ptr`. Grep sobre todo `src/`: el único `sm_map_erase` es `SessionManager.cpp:2386`.

**E-2 · El consumidor del colgante — `find` / `get_or_create`** `confirmado`
`src/network/raklib/SessionManager.cpp` — `mcp_rak_session_manager_find` `:158-169`, `mcp_rak_session_manager_get_or_create` `:171-194`:

```c
mcp_rak_session_t* mcp_rak_session_manager_find(...) {
    const uint64_t key = sm_ep_key(ip, port);
    ...
    while (sm->map_ptr[i]) {
        if (sm->map_key[i] == key)
            return sm->map_ptr[i];        // ← devuelve el puntero liberado
        i = (i + 1) & mask;
    }
    return NULL;
}
mcp_rak_session_t* mcp_rak_session_manager_get_or_create(...) {
    mcp_rak_session_t* s = mcp_rak_session_manager_find(sm, ip, port);
    if (s)
        return s;                          // ← reconexión = sesión muerta
    ...
    sm->sessions[sm->session_count++] = ns;
    sm_map_insert(sm, sm_ep_key(ip, port), ns);   // :192 — único insert
```

**E-3 · El sweep (único teardown coherente) itera el array** `confirmado`
`SessionManager.cpp:2267-2268, 2376-2389`:

```c
for (size_t i = 0; i < sm->session_count;) {
    mcp_rak_session_t* s = sm->sessions[i];      // ← itera el ARRAY
    ...
    if (should_remove) {
        sm_map_erase(sm, sm_ep_key(s->ip, s->port), s);  // :2386 erase emparejado
        mcp_rak_session_destroy(s);                       // :2387
        free(s);                                          // :2388
        sm->sessions[i] = sm->sessions[--sm->session_count]; // :2389
        continue;
    }
```

El huérfano no está en el array → el sweep nunca puede erasearlo. `manager_destroy` (`:70-82`) tampoco itera el mapa: lo zera con `memset(sm, 0, sizeof(*sm))` en el apagado.

**E-4 · `destroy` memetea antes del `free`** `confirmado`
`Session.cpp:85-128` — libera buffers internos (send queue `:89-92`, recovery `:94-97`, splits `:98-105`, order holds `:109-115`, backlog `:118-125`) y termina:

```c
    s->backlog_tail = 0;
    s->backlog_bytes = 0;
    memset(s, 0, sizeof(*s));      // :128
```

Consecuencia: en el primer acceso post-free sin reuso, el bloque contiene ceros (más los metadatos del allocator en los primeros 16-32 bytes). Las guardias leen `state == 0` (UNCONNECTED), no "3". No cambia el veredicto: la lectura ya es UB.

**E-5 · Tamaño del struct (medido)** `confirmado` — sonda de compilación (`g++ -std=c++20`, solo includes, ejecutada fuera del repo):

```
sizeof(mcp_rak_session_t) = 349848   // ~342 KB
```

Superior al umbral mmap inicial de glibc (128 KB): el primer `calloc` de sesión es mmap-backed → el primer `free` es `munmap` → el primer uso post-free puede ser **SIGSEGV**. Tras liberar un bloque mmap, glibc **sube dinámicamente el threshold** (comportamiento documentado de `malloc.c`, no medido en este binario) → las siguientes sesiones salen del heap → el mismo defecto pasa a corrupción silenciosa/aliasing. Comportamiento coherente con los arreglos del struct: `recovery[4096]` (`Session.h:146`), `order_hold[32][32]` (`:271`), `split_slots[32]` con `segments[256]` (`:275, :102`).

**E-6 · Código muerto irónico — la variante segura no tiene llamadores** `confirmado`
`ServerHandler.cpp:48-70` (`mcp_server_handler_send_encapsulated`) localiza por **array** (`:55-61`), lo que la haría inmune al huérfano; grep sobre `src/` confirma **cero llamadores** (el drenaje usa la variante por mapa, `RakLibInterface.cpp:233`).

**E-7 · Tabla de accesos post-free** (todas las rutas en el hilo RakLib; certeza de acceso `confirmado`, manifestación runtime dependiente del allocator):

| Ruta | Resolución colgante | Acceso | Líneas | Tipo |
|---|---|---|---|---|
| ACK (0xc0) | `find` `:2245` | `handle_ack`: diag/timestamps; `recovery[slot].data` | `:1885-1897, :1922-1924` | r/w + `free()` de puntero del bloque liberado |
| backlog tras ACK | `:2250` | `session_backlog_flush`: lee `backlog_count/backlog[]`, `free(data)` | `:238-265` | r/w + `free()` |
| NACK (0xa0) | `find` `:2254` | `handle_nack`: **envía por UDP** `recovery[slot].data` con `ip/port` del bloque liberado | `:1962-1970` | r/w + exfiltración potencial de heap |
| DATA (0x80-0x8F) | `find` `:2239` | `handle_data_packet`: `last_recv_seq`, `recv_window[]`, `ack_queue`, splits/order-holds (mallocs dentro del bloque liberado) | `:1462-1615` | r/w extenso |
| OCR1 (0x05) | `find` `:2014` | guardia lee `state` (`:2015`, UB); writes `mtu/state/last_activity` | `:2015-2058` | r + w |
| OCR2 (0x07) | `find` `:2067` | writes `mtu/client_id/state` (client_id: 8 bytes del cliente) | `:2104-2120` | r + w |
| send_encapsulated (envíos del juego) | `find` `:272` | guardia lee `state` (`:273`); backlog_push escribe mallocs; `send_payload` lee `mtu` | `:272-316`, `Session.cpp:417-510` | r + w |
| 0x13 CLIENT_HANDSHAKE | vía DATA | `state=CONNECTED` y `on_open_session` con `ipStr/port` del bloque liberado | `:1338-1356` | w + evento |

**E-8 · Ventana bad-batch** `confirmado` (ruta) / `probable` (ataque)
`Network.cpp:116-139` (hilo principal): decode fallido → `r->closeSession(ip, port, "bad batch")` (`:138`) **sin cerrar el Player** (`connected_` sigue true, no hay `SESSION_CLOSE`, nadie pone `pending_removal_`). Los eventos `PACKET_RECEIVED` del mismo endpoint ya encolados en `inbound_queue_` se procesan después (`drainEvents`, `RakLibInterface.cpp:295-337`) → `dispatchSingle` bloquea un Player vivo → responde → `SEND` encolado **detrás** del `CLOSE` → el drenaje FIFO de RakLib ejecuta `CLOSE` (free) y luego `SEND` → `find` colgante (`:272`). Da igual el entrelazado de hilos: el SEND llega a RakLib después del free en ambos ordenamientos.

**E-9 · Sin mitigaciones entre el free y los usos** `confirmado` (refutación activa del Agente 4)

| Mitigación candidata | Veredicto |
|---|---|
| ¿Despacho por array en vez de mapa? | No: los 7 consumidores usan el mapa |
| ¿Revalidación "¿está en el array?" tras `find`? | No existe en ninguna ruta viva (solo la función muerta E-6) |
| ¿Campo de generación/época? | No existe en `mcp_rak_session_t` (`Session.h:197-472`) |
| ¿El sweep purga el huérfano si el endpoint "vuelve"? | No: itera el array; `get_or_create` jamás re-inserta (retorna en `:174-175`) |
| ¿Guardias de estado protegen? | No: **leen memoria liberada para decidir** (UB); con reuso, evalúan basura |
| ¿Doble close protege? | Evita double-free del struct (no-op en array), no toca el mapa |
| ¿Anti-flood limita el abuso? | `MCP_SM_PACKET_LIMIT` muerto: se resetea (`:2221`), nunca se incrementa/comprueba (coincide con auditoría §12.3) |

---

## 5. Escenarios reproducibles

### S1 — Kick → ACK tardío (el más grave por trivialidad) `confirmado`

| Paso | Hilo | Acción | Evidencia |
|---|---|---|---|
| 1 | principal | `/kick` → `Player::kick`: `sendPacket(DisconnectPacket)` + `flushPendingPackets` | `Player.cpp:327-333` |
| 2 | principal | `closeSession` encola `CLOSE`; FIFO outbound queda `[SEND(disc), CLOSE]` | `Player.cpp:335-337` → `RakLibInterface.cpp:378-395` |
| 3 | principal | `close()` marca `connected_=false` / `pending_removal_` | `Player.cpp:340, :312` |
| 4 | RakLib (≤10 ms) | drena: SEND sale con sesión viva (RELIABLE → copiado a `recovery[]`, `Session.cpp:384-389`); CLOSE → **destroy+free+swap-remove, mapa colgante** | `RakLibInterface.cpp:190-242` → `ServerHandler.cpp:81-83` |
| 5 | RakLib | el cliente (como cualquier cliente RakNet) ACKea el datagrama del disconnect; 0xc0 llega → `find` → **puntero liberado** | `SessionManager.cpp:2243-2247` |
| 6 | RakLib | `handle_ack` escribe diag/timestamps en memoria liberada; lee `recovery[slot]` (ceros: el `free` interno no ejecuta en el primer ciclo) | `:1885-1897, :1922-1924` |

Comportamiento de cliente requerido: **ninguno especial** — el ACK normal en el instante del kick basta. Si el bloque era mmap: SIGSEGV en el paso 6. Si heap: UAF silencioso o, con reuso, aliasing (riesgo D).

### S2 — Reconexión desde la misma IP:puerto `confirmado` (ruta) / `probable` (frecuencia en producción)

1. Estado: `map[key_A] → P (liberado)`, P no está en `sessions[]`.
2. OCR1 del mismo endpoint → `find` devuelve P (`:2014`); guardia `:2015` lee memoria liberada; como P no es NULL, `:2037` se salta `get_or_create` → escribe `mtu/state=CONNECTING_1/last_activity` sobre memoria liberada (`:2056-2058`) y responde REPLY_1.
3. OCR2 → idéntico (`:2067`, writes `:2117-2120`, `client_id` controlado por el cliente).
4. DATA 0x09 → SERVER_HANDSHAKE (`:1308-1317`); DATA 0x13 → `state=CONNECTED` (`:1350`) y `on_open_session` con ip/port leídos del bloque liberado (`:1352-1356`).
5. El principal crea Player para la sesión (`Network.cpp:178-192, 310-328`) → **jugador "conectado" cuyo transporte entero corre sobre memoria liberada**; la sesión zombi nunca entra al array → nunca es reaped ni liberada.
6. Mientras el huérfano exista, **ninguna reconexión desde ese ip:puerto crea una sesión real**; la única vía de escape del cliente es otro puerto de origen.

NAT con preservación de puerto o cliente scriptado con rebind reproducen la ruta; un cliente típico con puerto efímero nuevo no la dispara (por eso S1 es el vector primario).

### S3 — Batch malformado + tráfico en vuelo `confirmado` (ruta) / `probable` (ataque remoto)

`Network.cpp:138` encola `CLOSE` sin tocar el Player (E-8) → los `PACKET_RECEIVED` ya encolados del mismo endpoint se procesan después → respuestas encoladas detrás del `CLOSE` → drenaje: free primero, `send_encapsulated` después → `find` colgante → guardia lee memoria liberada (`:273`) y, con bloque reutilizado, continúa hacia backlog/`send_payload`. Un cliente legítimo no lo genera; un atacante sí (sin autenticación de juego, solo handshake raklib de ~4 datagramas).

### Riesgos derivados

| Riesgo | Mecanismo | Certeza |
|---|---|---|
| D1 · Aliasing cruzado | `calloc` de `get_or_create` (`:187`) reutiliza el bloque de 350 KB para la sesión B → `key_A→P` y `key_B→P` coexisten; datagramas de A mutan la sesión viva de B; `handle_ack` de A libera buffers **vivos** de B (`:1923`) → free prematuro; doble free literal no se logró construir (NULL-after-free lo impide en las rutas alcanzables) | `probable` (depende del allocator) |
| D2 · free de puntero basura | mismo `:1923`/`:262` sobre bloque reciclado por otra asignación no-cero → `free()` de bytes arbitrarios | `probable` |
| D3 · Mapa lleno → **hilo RakLib congelado** | cada kick de endpoint distinto acumula 1 entrada inmortal; con L vivas, `1024−L` kicks llenan la tabla; el siguiente `find` con miss cicla eterno (`:163-167`, sin NULL que corte) → red muerta para todos los jugadores. Alcanzable sin login vía S3 | `confirmado` (comportamiento del código) / `probable` (DoS) |
| D4 · Double-free del struct | las 3 liberaciones operan solo sobre el array; destroy memetea antes de free | `descartado` |
| D5 · Estado huérfano funcional | kick-path no emite `SESSION_CLOSE` → `Network::sessions_` queda con weak_ptr muerto hasta sobreescritura por nuevo login (`Network.cpp:340`) | `confirmado` (no es de memoria) |

---

## 6. Causa raíz

1. **Doble registro con contrato manual.** El conjunto de sesiones vive en dos estructuras con responsabilidades distintas (array propietario para iteración/liberación; tabla hash plana para lookup O(1)) y el invariante "ambos permanecen coherentes" solo está escrito en los comentarios (`SessionManager.h:68-73`). Hay **dos** implementaciones del teardown (sweep `:2376-2389` y `close_session` `ServerHandler.cpp:72-87`); una implementó las dos mitades, la otra solo una.
2. **Regresión de la migración al índice O(1)** (`11bd1d3`): el fix C1 previo de memoria introdujo el destroy correcto en el kick path, pero la nueva indexación no integró `ServerHandler` — el único teardown que no vive en `SessionManager.cpp` es precisamente el que se quedó sin la mitad nueva del contrato.
3. **Encapsulación insuficiente:** `sm_map_erase`/`sm_ep_key` son `static` de `SessionManager.cpp` (`:109-156`) y no hay API pública de borrado (`SessionManager.h:78-106` no la declara) → el autor de `ServerHandler` *no podía* cumplir el contrato sin duplicar el algoritmo; el defecto era la consecuencia natural de la estructura.
4. **Falta de test del invariante:** ningún harness verifica `map_entries == session_count` ni churn kick/reconnect bajo ASan (`memory_audit_checks.py` reconecta con socket nuevo → puerto nuevo → nunca resolvió el mismo endpoint).
5. **Agravantes:** el registro muerto por-array (E-6) habría mitigado el envío; el anti-flood declarado está sin aplicar (E-9); `MCP_SM_MAX_SESSIONS` acota sesiones vivas pero nada acota entradas huérfanas (D3).

Coincide con la lectura de la auditoría (§12.5): la convención create/destroy sin RAII convierte un olvido local en corrupción de proceso; este caso es la demostración empírica.

---

## 7. Solución recomendada (diseño; NO implementada)

Restricciones respetadas por todas las opciones: sin copias nuevas por paquete, sin bloqueos adicionales, sin cambios de protocolo/wire-format, todo dentro del hilo RakLib.

### (a) Fix literal "una línea" de la auditoría — `no viable tal cual`
`sm_map_erase` y `sm_ep_key` son `static` (`SessionManager.cpp:129, :109`): `ServerHandler.cpp` no puede llamarlas. Forma mínima correcta: exponer en `SessionManager.h` un wrapper (`mcp_rak_session_manager_map_erase_endpoint(sm, ip, port)`) implementado dentro de `SessionManager.cpp`, y llamarlo entre `:81` y `:82` con el **mismo puntero** que se va a liberar (el erase valida `(key, ptr)`).
- **Pros:** diff mínimo; O(1) por kick; cero coste por paquete.
- **Contras:** el contrato sigue duplicado en dos sitios → la clase de error sobrevive; no protege contra futuras rutas de destrucción.

### (b) ⭐ Hotfix recomendado — teardown consolidado en el dueño
Nueva función pública **`mcp_rak_session_manager_close(mcp_rak_session_manager_t* sm, const uint8_t ip[4], uint16_t port)`** implementada en `SessionManager.cpp`:
1. Localizar la sesión **escaneando el array** (no el mapa) — corrección crítica del verificador: tras un aliasing D1, localizar por mapa podría destruir la sesión **viva** de otro jugador que ocupa el mismo bloque.
2. `sm_map_erase(sm, sm_ep_key(s->ip, s->port), s)` con ese puntero exacto.
3. `mcp_rak_session_destroy(s)` + `free(s)` + swap-remove del array (el código actual de `ServerHandler.cpp:76-86`, trasladado al dueño).
`ServerHandler::close_session` se reduce a delegar; el sweep (`:2376-2389`) reutiliza la misma primitiva conservando su diferencia (emitir `on_close_session` solo en timeout, `:2377-2384`).
- **Pros:** dueño único del contrato → elimina la clase de regresión que produjo el bug; el invariante deja de poder romperse por olvido; diff acotado (3 archivos); coste O(n≤512) **una vez por kick** (despreciable; hoy ya es O(n)).
- **Contras:** refactoriza el sweep (cuidado con el callback diferido); no elimina el doble registro.

### (c) ⭐ Rediseño recomendado — dueño único RAII
Sustituir array+mapa por **`std::unordered_map<uint64_t, std::unique_ptr<McpRakSession>>`** como único registro (borrar = destructor; un huérfano es estructuralmente imposible). El cap pasa a `map.size()`; el sweep itera el mapa; el escaneo por-array de `ServerHandler.cpp:55-61` desaparece con el código muerto.
- **Pros:** la clase de error desaparece por construcción; menos invariantes manuales; alineado con la dirección RAII ya recomendada por la auditoría (§9.4, §13.5).
- **Contras:** nodo-based con pointer-chasing por lookup frente al sondeo lineal actual sobre arrays planos contiguos (1-2 cargas de caché) → coste real en el hot path por datagrama (mitigable: ≤512 sesiones, lookup una vez por datagrama); un alloc de nodo por sesión; el sweep pierde el swap-remove O(1) (ya es O(n)); migración mecanizada de ~30 funciones `mcp_*`.

### (d) Generación/época — cinturón, no solución
Stamp `uint32 gen` en la sesión + junto al puntero del mapa; cada consumidor valida. Convierte olvidos futuros en rechazos detectables a coste de 1 comparación, pero **no purga el huérfano** (D3 persiste) y exige disciplina en 7 sitios. Útil solo como refuerzo transitorio durante la migración (b)→(c).

**Recomendación final:** (b) ahora, (c) en la fase de consolidación RAII, con (d) opcional entre ambas.

---

## 8. Archivos que necesitarían modificarse en una futura implementación

| Archivo | Cambio para el hotfix (b) | Cambio para el rediseño (c) |
|---|---|---|
| `src/network/raklib/SessionManager.h` | Declarar `mcp_rak_session_manager_close()` (y opcionalmente contador de entradas para tests) | Sustituir array+mapa por el mapa RAII en el struct del manager; ajustar API |
| `src/network/raklib/SessionManager.cpp` | Implementar `manager_close` (find-por-array + erase + destroy + free + swap-remove); refactorizar sweep `:2376-2389` para delegar | Reescribir `get_or_create/find/tick/sweep/destroy` sobre el mapa; eliminar `sm_map_*` |
| `src/network/raklib/ServerHandler.cpp` | `close_session` delega en `manager_close`; (opcional) eliminar el código muerto `:40-70` | Igual |
| `src/network/raklib/ServerHandler.h` | Sin cambios | Ajustar firma si el manager cambia de tipo |
| `src/network/raklib/Session.h` / `Session.cpp` | Sin cambios | Convertir a clase RAII (destructor = `destroy`) |
| `src/network/raklib/RakLibServer.h/.cpp` | Sin cambios | Tipos del manager en la struct del servidor |
| `tools/` (nuevo harness de churn) | Añadir escenario kick/reconnect + mapa lleno (§9) | Igual |
| **No tocar** | `RakLibInterface.*` (su API de colas ya es correcta), protocolo 84, `Network.cpp` (salvo que se quiera cerrar el Player en la ruta bad-batch, mejora funcional independiente) | — |

---

## 9. Pruebas recomendadas para verificar la corrección

1. **Harness de churn kick/reconnect** (sobre `tools/memory_audit_checks.py` y el `Client` de `tools/linux_login_check.py`, que ya hacen handshake+login raknet completos):
   - Escenario S1: conectar → login → generar tráfico fiable (chunks) → `kick` → **en la ventana RTT**, desde el mismo socket, enviar 0xc0 ACKeando los seq vistos de los datagramas 0x84 del servidor, y después 0x80/0x84 tardíos. ×20-50 iteraciones.
   - Escenario S2: re-enchufar **re-bindeando el mismo puerto local** (`bind()` + `SO_REUSEADDR` — el Client actual no bindea, hay que añadirlo) y repetir OCR1→OCR2→0x09→0x13→login.
   - Escenario S3: batch con zlib corrupta (el caso `sc_zlib_bomb` existente ya pasa por "bad batch → close on purpose", `memory_audit_checks.py:113-114`, pero reconecta con socket nuevo → puerto nuevo → por eso nunca atrapó el UAF) seguido de un paquete de juego que elicite respuesta, sin cerrar el socket.
   - Escenario D3: N≥1024 endpoints distintos (puertos efímeros) con handshake+bad-batch, luego un ping de endpoint fresco con **watchdog de 10 s** (pre-fix: hang de `find`; post-fix: respuesta normal).
2. **AddressSanitizer + UBSan** (`-fsanitize=address,undefined -fno-omit-frame-pointer -g`): punto clave — ASan **sustituye** el allocator de glibc, pone el bloque de ~350 KB liberado en cuarentena envenenada y nunca lo des-mapea → **el UAF se detecta siempre, en el primer acceso**, sin que el threshold mmap/munmap lo enmascare.
3. **Salida ASan esperada:** `heap-use-after-free READ of size 1` en `handle_ack` (`SessionManager.cpp:1885/1894`) o `WRITE` en `:2056-2058`/`:2117-2120`/READ en `:273`, con stack `freed by: mcp_server_handler_close_session ServerHandler.cpp:82 ← RakLibInterface.cpp:237` y `allocated by: calloc get_or_create SessionManager.cpp:187`. Variantes aceptables: `attempting free on address which was not malloc()-ed` (D1/D2) o `double-free`. Para D3 no hay reporte: hay **hang** (cubrirlo con el watchdog).
4. **Test de invariante permanente (CI):** unit test de `SessionManager` que tras cada `manager_close` verifique `find(ep) == NULL` y `entradas_de_mapa == session_count` (el invariante que hoy se rompe); exponer un contador solo-test si hace falta.
5. **Gate de integración:** el harness de churn como test gateado a build ASan (fallo = exit ≠ 0 de ASan) + aserción conductual "un endpoint fresco sigue pudiendo conectar tras K kicks".

**Nota de honestidad:** en esta investigación **no se ejecutó** el servidor, ASan ni ningún harness (mandato de solo lectura). Todo lo anterior es diseño de verificación, no resultado de corrida.

---

## 10. Lo que no pudo verificarse (limitaciones)

1. **Manifestación runtime exacta** (SIGSEGV por munmap vs corrupción silenciosa vs aliasing): depende del allocator y del threshold dinámico de glibc en el binario real — razonada sobre `sizeof` medido (349.848 B) y comportamiento documentado de glibc, **no medida en ejecución**.
2. **Frecuencia de reuso de ip:puerto por clientes reales** (NAT con preservación de puerto): determina cuán a menudo dispara S2 en producción; la ruta de código está confirmada, la frecuencia no es medible por lectura.
3. **Salida textual exacta de ASan y valores de punteros reciclados** (D1/D2): dependen del heap en runtime.
4. **`Session.cpp` fuera de los tramos citados** (send_payload/recovery/split en detalle completo) y cuerpos de workers async del game layer: revisados solo en los tramos que afectan al ciclo de vida del struct; no alteran el veredicto.
5. La medición de `sizeof` se hizo con una sonda de compilación en `/tmp` (única acción fuera de lectura pura; sin tocar el repositorio); el Agente 4 la reprodujo por aritmética de campos (≈350 KB, ±0,1 %) con convergencia en la conclusión (>128 KB).

---

## 11. Conclusión del coordinador

Los cuatro agentes convergen y las dos contradicciones iniciales (contenido post-free del struct; ventana SEND-después-de-CLOSE) se resolvieron con lectura directa del código: `destroy` memetea antes del `free` (`Session.cpp:128`) y la ventana existe por la ruta bad-batch (`Network.cpp:138`), no por el kick. El UAF de §12.1 es **real, confirmado y monohilo**: `ServerHandler.cpp:81-84` libera sin borrar del índice, el único erase vive en un sweep que itera el array, y no existe ningún mecanismo de purga hasta el apagado. Cada kick/ban/bad-batch deja un endpoint envenenado permanentemente y convierte el siguiente datagrama de ese endpoint —incluido el ACK que el propio cliente manda del paquete de desconexión— en acceso a memoria liberada, con reconexión-zombi, aliasing cruzado y un DoS de mapa lleno como agravantes demostrables. El hotfix correcto no es "una línea" sino **una función**: consolidar el teardown en el dueño del índice con localización por array; el rediseño RAII de dueño único es la única opción que elimina la clase del error.
