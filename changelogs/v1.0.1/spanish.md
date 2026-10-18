# CurseStone v1.0.1 Beta Release

### Añadido
- Añadido un sistema de plugins basado en Lua 5.4, con manifiestos, enlaces de ejecución, analizadores YAML y JSON integrados y el comando `/plugins`.
- Añadidos nodos de permisos para plugins, generación dinámica de `/help` y permisos predeterminados `cursestone.*` para operadores.
- Añadidos nuevos biomas, ruido de terreno similar al de vanilla, generación de cuevas y lagos, distintos tipos de árboles, aldeas y templos del desierto.
- Añadidos el sistema de hambre, la sincronización de salud, el daño ambiental y las animaciones de agacharse y correr de los jugadores.
- Añadidos el manejo de la reaparición, skins predeterminadas para clientes sin skin, iluminación de antorchas y lava, y estados encendido y apagado de los hornos.
- Añadida la persistencia del contenido de los cofres y de las entidades de bloque de cofres y hornos en estructuras generadas.
- Añadidos un sistema compartido de conversión de objetos a NBT, física de movimiento mediante Swept AABB, trazado de rayos y diagnósticos de fallos mejorados.
- Añadidos informes de errores de paquetes, soporte de compilación independiente mediante `build_local.sh`, comprobaciones de desarrollo, pruebas de rendimiento y encabezados de licencia en los archivos fuente.

### Cambiado
- Renombrada la API `Level` a `World` en todo el código base, incluyendo eventos de mundo, claves de configuración y directorios del código fuente.
- Actualizadas las versiones del servidor y de la API de plugins a `1.0.1`.
- Dividida la funcionalidad de los jugadores en módulos de código fuente independientes.
- Trasladado el registro de recetas de fabricación y del inventario creativo al código, en lugar de utilizar recursos JSON.
- Mejorados la planificación de los ciclos del servidor, la generación del terreno del Nether y los límites de transmisión de chunks.
- Reducido a 1400 el valor predeterminado del MTU de RakNet.
- Reescrita la documentación README y CONTRIBUTING.

### Corregido
- Corregidos múltiples problemas de fiabilidad de RakNet relacionados con mensajes duplicados, establecimiento de conexiones, inicio de sesión, retransmisiones y tiempos de espera.
- Mejorada la estabilidad de las conexiones para reducir la posibilidad de que un jugador bloqueado o inactivo afecte a los demás.
- Corregidos el análisis de paquetes de inicio de sesión en Linux, los identificadores de paquetes de recogida de objetos, la codificación de actualizaciones de bloques, la sincronización de skins y el manejo de las acciones de carrera.
- Corregidos los cierres inesperados causados por carteles con líneas vacías y escrituras fuera de los límites del texto de los carteles.
- Reducidos los bloqueos del hilo principal durante el guardado automático.

### Eliminado
- Eliminados los recursos JSON obsoletos de recetas, objetos del inventario creativo y biomas.
- Eliminados el antiguo directorio de código fuente `Level`, la implementación de `SimplexNoise` y los archivos heredados `main.cpp` y `main.h`.
- Eliminada la lista de comandos codificada directamente que se utilizaba anteriormente en `/help`.