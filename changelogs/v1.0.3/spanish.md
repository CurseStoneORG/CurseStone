# CurseStone v1.0.3 Beta Release

Cambios desde la versión Beta v1.0.2.

### Añadido
- Portado el generador de mundo principal vanilla de MCPE 1.0.0 y eliminado el generador anterior: distribución de biomas sólida, relieve natural del terreno, reglas de supervivencia de plantas vanilla y el tinte de hierba de las colisas extremas.
- Ríos vanilla que tallan su propio lecho y valles, incluyendo la caída de ríos en tierras altas, y superficies de mesa pintadas con arcilla endurecida y bandas de colores.
- Cofres dobles vanilla con ventanas de contenedor de 54 espacios, espectadores sincronizados y caída de objetos por mitad al romperlos.
- Los efectos de partículas de polvo de aterrizaje y de sprint que el cliente 0.15 nunca mostró.
- Metadatos de entidades completos visibles para el cliente y sincronización de experiencia (XP).
- Soporte de compilación Linux ARM64 (Cobalt 100) y empaquetado de versiones, junto a los archivos existentes de Linux x86-64, Android ARM64 y Windows x86-64.
- Nombre del servidor en la lista de servidores configurable en `cursestone.yml`.
- Un informe de cliente perdido escrito cuando una sesión muere sin una desconexión limpia, facilitando el diagnóstico de red.
- El coste de luz por tick mostrado en la salida de diagnóstico de `/status`.
- `cursestone-bot`, un cliente de pruebas con la forma real del protocolo 84 usado para ejercitar el inicio de sesión, el envío de chunks, los contenedores y la interacción con hornos de extremo a extremo, incluyendo un escenario donde el bot toca un horno natural de una aldea.
- Un informe de investigación del use-after-free de sesiones de RakLib y un documento de auditoría de la arquitectura C++ nativa.
- Un nuevo logo del proyecto.

### Cambiado
- Desactivada la generación de hornos en el generador de mundos vanilla mientras se investiga un crash provocado al tocar un horno generado naturalmente.
- Pulido del regenerado de mundo: ríos vanilla anchos, ruido de profundidad de superficie consistente muestreado en coordenadas de bloque del mundo para que el terreno coincida entre los límites de chunks, y cada paso de poblamiento ahora trabaja sobre una copia privada del vecindario 3x3.
- Portado el motor de flujo de líquidos vanilla 1.0.0: el agua y la lava generadas llegan asentadas, los líquidos quietos guardados despiertan correctamente al cargar su chunk y las tormentas de actualizaciones cerca del agua desaparecieron.
- Envío de chunks fluido con trabajo por tick acotado, y reenvío de los chunks que el cliente descarta cuando cambia su radio de renderizado.
- Actualización incremental del mapa de alturas del generador al escribir bloques, en lugar de reconstruirlo.
- Minado vanilla: los drops dependen de la herramienta correcta y la velocidad de rotura se verifica en el servidor.
- Migrada la capa C propia a C++ nativo: NBT es ahora una API de clases nativa sin enlazado C, handles RAII protegen los buffers del límite C y las etiquetas NBT, el motor de luz consulta el registro de bloques directamente y las sesiones RakLib se indexan en O(1).
- Relights de luz en tormentas de chunks 3 veces más baratos con resultados idénticos, menos asignaciones por poblamiento y reescaneos de estructuras, menor coste del hilo de tick en el envío de chunks y el batching, chat codificado una vez por difusión con límite de frecuencia, y paquetes rechazados por congestión encolados.
- El progreso del horno solo se envía mientras el fuego arde, y el reenvío de NBT de tiles se eliminó del lote de apertura de contenedores.
- Toda la depuración de consola queda tras el modo debug.
- Binarios con símbolos de depuración y backtraces de crash simbolizados, y eliminados los volcados de sesión permanentes.
- Ciclo de vida de memoria, límites de asignación y orden de apagado reforzados.
- Actualizada la versión del servidor y de la API de plugins mediante `CURSESTONE_API`, establecida en `1.0.3`.
- Normalizada la cabecera de todos los archivos al banner de CurseStone, traducidos los comentarios y líneas de log al inglés, revisado el README, y eliminadas 152 cabeceras nunca incluidas, paquetes de transporte muertos, mobs no generados, eventos huérfanos y stubs obsoletos.

### Corregido
- Corregido el uso tal cual de archivos `server.properties` incompletos: un archivo que existe pero está vacío o solo contiene un par de opciones (por ejemplo, los valores reducidos que traen paneles de hosting como Pterodactyl) ahora se completa con todas las opciones faltantes en lugar de usarse tal cual.
- Corregido un use-after-free de sesiones de RakLib con un único teardown del propietario, y una propiedad de TagGuard invertida que hacía doble free de listas NBT guardadas.
- Corregida la interacción con hornos para el cliente 0.15: los hornos se abren como ventanas de contenedor genéricas, el horno se abre antes del manejo del objeto en mano, y el compuesto de spawn completo del horno se envía.
- Corregida la animación de la tapa de los cofres con el evento de bloque de la época correcta.
- Corregida la desincronización del cliente al cancelar un plugin una edición de bloque: ahora se le dice la verdad al cliente cuando un plugin cancela su edición de bloque, y los read-back vacíos en los hooks de eventos de Lua están protegidos.
- Corregida la superposición de quemado que no aparecía en los clientes.
- Corregido que el fuego solo se apagaba con caras sumergidas; el agua ahora apaga el fuego al tocarlo de cualquier forma.
- Corregida la sincronización del tiempo fijando el reloj del mundo donde el cliente realmente lo vincula.
- Corregida la lista de jugadores que mantenía entradas de jugadores que ya se habían ido.
- Corregido el flujo de apertura de contenedores para que coincida con la referencia del protocolo 84, respetados los ids de ventana al cerrar contenedores, y difusión de los cambios de slot a cada co-espectador.
- Corregida la pérdida de actores de tiles al reenviar chunks: los tiles se reescanean en cada envío para que los bloques reenviados conserven sus cofres, hornos y letreros.
- Corregido un desajuste del índice nibble de sección que corrompía datos de bloques.
- Corregidas carreras en las capas de biomas, árboles cortados en los límites de chunks y datos de troncos incorrectos.
- Corregida la colocación de bloques dentro de las hitboxes de entidades.
- Corregidos los vertidos y llenados de cubos en el formato de cable de cubos de la 0.15.
- Corregida la caída del contenido de contenedores en todos los modos de juego y eliminado el jitter del imán de objetos.
- Corregido el comportamiento de las capas de nieve al despertar líquidos generados.
- Corregido el combate creativo: retroceso de ataque, flash de daño en la vista propia de la víctima, pose de arco al golpear bloques, flashes fantasma de flechas cerca de jugadores, y reenlaces de la hotbar para que el equipamiento creativo surta efecto correctamente.
- Corregido el bucle de espera de stdin del lector de consola en Windows y hecho que la salida del proceso ya no se bloquee por el lector de consola.
- Corregida la compilación en Windows con MSVC: literales compuestos C99 en Chunk.cpp y una ambigüedad de igualdad de BlockID señalada por clang y MSVC.
- Corregidos los disparadores de CI y eliminada la dependencia opcional de LevelDB.
- Corregida la ruta de include del generador faltante en `build_local.sh`.

### Notas
- Comparado contra la Beta v1.0.2.
- La generación de hornos en el generador de mundos vanilla está desactivada porque se está investigando un crash al tocar un horno generado naturalmente.
- Las estructuras generadas actualmente no contienen cofres; los cofres se añadirán en una actualización futura.
- Esta actualización no incluye cambios importantes en la API de plugins.
- Cliente objetivo: Minecraft: Pocket Edition 0.15.x.
- Protocolo de red: 84.
- CurseStone es un software de servidor y no implementa todas las mecánicas de Minecraft vanilla.
- Algunas características pueden requerir pruebas adicionales dentro del juego para verificar su compatibilidad completa con el cliente.
