# CurseStone v1.0.2 Beta Release

Cambios desde la versión Beta v1.0.1.5.

### Añadido
- Añadido soporte de compilación para Windows x86-64, compatibilidad con MSVC, scripts de inicio para Windows y archivos de distribución específicos para cada plataforma.
- Añadidos portales del Nether, vinculación de portales, conversión de coordenadas y viajes entre el mundo principal y el Nether.
- Añadida la gestión de múltiples mundos, actualización independiente de cada mundo, carga y descarga de mundos y guardado automático.
- Mejorada la generación del terreno del Nether, incluyendo límites de bedrock, agrupaciones de piedra luminosa y colocación de fuego.
- Añadidas camas con mecánicas similares a las de Minecraft vanilla, animaciones al dormir, puntos de aparición, sincronización entre jugadores y avance de la noche cuando todos los jugadores de un mundo están durmiendo.
- Añadidos el hambre y las mecánicas de alimentación, incluyendo saturación, agotamiento, regeneración, inanición y persistencia de los datos del jugador.
- Añadidos alimentos, incluyendo patatas asadas, remolacha, sopa de remolacha, pescado, cordero, conejo, estofado de conejo, galletas, pastel de calabaza, zanahorias doradas, manzanas doradas encantadas, carne podrida y ojos de araña.
- Añadidas pociones bebibles y arrojadizas, colisiones de proyectiles, efectos de rotura y aplicación de efectos en áreas.
- Añadidos 23 efectos de estado: Velocidad, Lentitud, Prisa, Fatiga minera, Fuerza, Salud instantánea, Daño instantáneo, Salto mejorado, Náuseas, Regeneración, Resistencia, Resistencia al fuego, Respiración acuática, Invisibilidad, Ceguera, Visión nocturna, Hambre, Debilidad, Veneno, Wither, Aumento de salud, Absorción y Saturación.
- Añadidos cubos de leche que eliminan los efectos de estado activos y dejan un cubo vacío.
- Añadidas las mecánicas de tensar y disparar arcos, el vuelo de las flechas, las colisiones de proyectiles, la recogida de flechas y su desaparición automática.
- Añadida física de bloques que caen para arena, grava y yunques.
- Añadidos nombres personalizados para los objetos y mejorada la conservación de datos NBT en inventarios, equipamiento, contenedores y objetos soltados.
- Ampliada la API de plugins Lua con gestión de mundos, manipulación de jugadores, acceso a recursos y eventos adicionales relacionados con jugadores, entidades, camas, puertas e interacciones.
- Añadida la posibilidad de reemplazar comandos mediante Lua, restaurando automáticamente los comandos originales cuando se descargan los plugins.
- Añadidos límites de ejecución para Lua y desactivación automática de plugins tras errores repetidos o tiempos de ejecución excesivos.
- Añadidos los comandos `/kill <player>` y `/status`, este último exclusivo para operadores y con información sobre TPS, memoria, tiempo de actividad, jugadores, plugins y mundos.
- Añadidos un asistente de configuración inicial, plantillas de configuración predeterminadas integradas y scripts de inicio para Linux, Termux y Windows.
- Añadida la generación de documentación con Doxygen, publicación automatizada, directrices de desarrollo y reglas de formato del código.

### Cambiado
- Separadas las opciones básicas de `server.properties` de las opciones avanzadas de `cursestone.yml`.
- Trasladado el almacenamiento de datos de los plugins al directorio `data_pl/` en la raíz del servidor.
- Actualizadas las versiones del servidor y de la API de plugins a `1.0.2`.
- Mejorados los cálculos de daño ambiental, armadura, modo creativo, resistencia, fuerza, debilidad y absorción.
- Mejorada la sincronización multijugador de la salud, el hambre, el consumo de alimentos, el equipamiento, el uso de objetos, los arcos, las flechas, el retroceso, la muerte, la reaparición y la recogida de objetos.
- Mejorada la persistencia de los datos del jugador, incluyendo salud, hambre, saturación, agotamiento, efectos activos y puntos de aparición establecidos mediante camas.
- Optimizado el guardado incremental automático de los chunks en los mundos cargados.
- Mejoradas la validación de las pilas de objetos, las cantidades máximas y la combinación de objetos teniendo en cuenta sus datos NBT.
- Mejorada la compatibilidad de Windows y enlazadas estáticamente las bibliotecas zlib y LevelDB para evitar dependencias de DLL ausentes durante la ejecución.
- Estandarizados el formato del código fuente y los comentarios; eliminados archivos de código obsoletos y utilidades de pruebas desactualizadas.

### Corregido
- Corregidos los problemas de detección, activación, metadatos y vinculación de portales del Nether, así como los viajes entre dimensiones.
- Corregidos los problemas que provocaban que las camas y puertas se convirtieran en bloques fantasma o no se sincronizaran correctamente.
- Corregidas las animaciones al dormir y la sincronización de los datos de sueño.
- Corregidos el envío de chunks y el orden de los paquetes durante el inicio de sesión para mejorar la entrega inicial del terreno.
- Corregidos los identificadores de entidades de red de los bloques que caen y su representación en el cliente.
- Corregidas las interacciones con el fuego, el daño ambiental y el daño por caída.
- Corregidos la representación de las pociones, los efectos de rotura de las pociones arrojadizas y la sincronización de efectos de estado en MCPE 0.15.10.
- Corregidos los problemas multijugador relacionados con el consumo de alimentos, el uso de leche, las actualizaciones de salud y la sincronización del hambre.
- Corregidos los problemas multijugador con las animaciones de los arcos, la aparición de flechas, el movimiento de proyectiles y el disparo.
- Corregidos los objetos soltados que permanecían visibles después de ser recogidos y mejorada la sincronización de objetos entre jugadores.
- Corregidos la duplicación de objetos, los tamaños de pila no válidos y la combinación incorrecta de objetos con diferentes datos NBT.
- Corregida la pérdida de nombres personalizados y datos NBT durante las operaciones de inventario, contenedores, equipamiento y objetos soltados al morir.
- Corregidos el manejo de la muerte y reaparición de los jugadores, así como el procesamiento de paquetes de eventos de bloques de cofres.
- Corregidos los enlaces de eventos de plugins y el manejo de mensajes.
- Corregido el análisis de la lista de operadores en Android, incluyendo archivos UTF-16 y formatos inconsistentes de las entradas.
- Corregidos problemas de compilación para Windows, gestión de hilos, compatibilidad POSIX y empaquetado de versiones.
- Corregidos los valores predeterminados de la configuración inicial y el comportamiento cuando faltan archivos de configuración.

### Notas
- Cliente objetivo: Minecraft: Pocket Edition 0.15.x.
- Protocolo de red: 84.
- CurseStone es un software de servidor y no implementa todas las mecánicas de Minecraft vanilla.
- Algunas características pueden requerir pruebas adicionales dentro del juego para verificar su compatibilidad completa con el cliente.