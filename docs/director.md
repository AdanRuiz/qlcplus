# Director: el show se opera solo

El Director arranca y detiene funciones de QLC+ según la música. Usa las funciones que genera el Show Wizard y la información musical del plugin VertexAV Link. Incluye un modo demostración para ver el show sin música, sin fixtures y sin emisor.

Estado: mínimo viable en la rama `vertexav-director`. Solo interfaz QML (QLC+ 5).

## Uso en tres pasos

1. **Monta el rig.** Agrega tus fixtures al proyecto.
2. **Pasa el Show Wizard** (icono del sombrero en el panel de funciones). En el resumen deja marcada la opción «Let the Director run the show» y pulsa generar.
3. **Abre el panel del Director** con el icono de la varita en la barra superior. Pulsa «Start demo» y abre la vista 2D o 3D para ver el show.

Con el emisor de VertexAV conectado no hace falta la demostración: el Director sigue a la música en cuanto recibe señal.

## Qué decide el Director

El Director mantiene cuatro capas y en cada una corre un grupo de funciones a la vez.

| Capa | Qué pone | De dónde sale |
| --- | --- | --- |
| Base | Color de fondo en cada grupo | Escenas que crea el Director con sus tres paletas |
| Movement | Movimiento de cabezas móviles | Circle Chase, Figure Eight y Audience Sweep del wizard |
| Rhythm | Matrices y chasers | Wave, Pixel Chase, Marquee, Fireworks, Plasma, Rainbow, Split Color, Heartbeat |
| Accent | Strobes y blinders | Escenas de strobe del Director y Blinder Hit del wizard |

Una regla dice qué grupos de funciones son candidatos en cada capa para una sección musical. Gana la regla más específica. Los cambios se aplican en el primer tiempo del compás, y dentro de una regla los candidatos rotan cada cierto número de compases.

Reglas por defecto:

| Regla | Secciones | Rota cada |
| --- | --- | --- |
| Silence | silence | nunca |
| Calm | intro, outro, breakdown | 8 compases |
| Verse | verse, bridge | 8 compases |
| Chorus | chorus | 8 compases |
| Build | build | 4 compases |
| Drop | drop | 4 compases |
| Default | cualquier otra | 8 compases |

Una regla solo se crea si el rig tiene algo que poner en ella.

La intensidad musical (low, medium, high, peak) regula el nivel de la capa Base: 45 %, 70 %, 90 % y 100 %.

La capa Accent es un golpe, no un estado: cuando la música lo permite (canal «strobe ok» de VertexAV) se enciende durante el primer compás de la regla y de cada rotación. El botón «Strobe» del panel la mantiene mientras se pulsa. Nunca se enciende si el operador bloqueó los strobes.

Como acento solo se usan las escenas de strobe de los grupos con canal de shutter y el «Blinder Hit» de los grupos con rol Blinder. Un rig sin ninguno de los dos no tiene acentos.

### Barras motorizadas (tilt sin pan)

El wizard solo genera movimiento para equipos con canal de pan. A los equipos que tienen tilt pero no pan, como una barra de píxeles motorizada, el Director les crea un chaser «<grupo> - Tilt sweep»: un barrido lento entre los valores 72 y 184 del canal de tilt. Corre en la capa Movement junto a lo que hagan las cabezas móviles, o solo si no las hay, y descansa en la regla de silencio.

## Colores

El Director crea tres paletas de color: «Director - Primary», «Director - Secondary» y «Director - Accent». Los tres primeros colores que envía VertexAV se escriben en ellas al empezar el compás, y la capa Base se reinicia para tomarlos. Un color negro se interpreta como «sin usar» y no cambia la paleta.

## Panel

- **AUTO / OFF**: enciende o apaga el Director. Al apagarlo se detienen solo las funciones que él arrancó.
- **Music**: BPM, tiempo del compás, sección, intensidad, energía, paleta recibida y permiso de strobe. Sección e intensidad se muestran en ámbar cuando están forzadas.
- **Layers**: qué corre en cada capa. «Pin» deja la capa como está aunque cambie la música. «Off» la apaga.
- **Overrides**: forzar sección o intensidad, «Hold» para congelar todo, «Next look» para pasar al siguiente candidato, «Blackout» y «Strobe» mientras se mantienen pulsados, y bloquear los strobes. «Release all» devuelve el control a la música.
- **Demo**: una canción sintética en bucle (intro, verse, build, drop, breakdown, build, drop, outro). Se puede cambiar el BPM y saltar a una sección, que entra en el siguiente compás.

## Qué se guarda en el proyecto

Las reglas, las paletas asignadas, si el Director está encendido y el permiso de strobes se guardan en un bloque `<Director>` dentro del archivo `.qxw`. Un QLC+ sin Director abre el archivo, avisa de una etiqueta desconocida y pierde ese bloque si vuelve a guardar. Si el proyecto no tiene reglas, no se escribe nada.

Los overrides (forzados, pin, off, hold) son estado en vivo y no se guardan.

## Límites actuales

- Las reglas no se pueden editar desde la interfaz; se generan con el wizard. Volver a pasar el wizard las reemplaza.
- El bloque de strobes del panel de overrides (velocidad, tipo, color, roles, límite de tiempo) no está hecho: hay un botón momentáneo y el bloqueo.
- Las cuentas regresivas antes del drop, las transiciones de mezcla, el mood y los disparadores de VertexAV se reciben pero no se usan.
- Los EFX y varios chasers del wizard usan valores por fixture. Al cambiar de rig hay que volver a pasar el wizard.
- Los textos nuevos están en inglés, sin traducir.
- Las capas se suman: un chaser de color de la capa Rhythm y el color de la capa Base escriben en los mismos canales y se mezclan por valor más alto.
- La demostración sí envía DMX a las salidas configuradas; no hay todavía un interruptor para evitarlo.

## Cómo se probó

Todo se ejecuta con `qmlui/director/test/run-tests.sh` dentro de un contenedor Linux (Ubuntu 25.04, Qt 6.8.3). El `Dockerfile` está en la misma carpeta.

| Comprobación | Qué cubre |
| --- | --- |
| Compilación de `qlcplus5` | La aplicación completa compila y enlaza con el Director |
| `directorengine_test` | 14 casos del núcleo: reglas, rotación, acentos, pin, off, hold, blackout, forzados |
| `directormanager_test` | 10 casos contra un `Doc` real: reglas por defecto, demostración, overrides, guardado y carga, borrado de funciones |
| `director_e2e` | La aplicación real sin pantalla: parchea 4 pares y 4 cabezas móviles, ejecuta el Show Wizard real, arranca la demostración y comprueba que el Director corre funciones del wizard y que el panel se carga en la vista principal |
| `directorpanel_preview` | Carga el panel con un Director en marcha, falla si hay avisos de QML y guarda una captura |

`director_e2e` acepta además `DIRECTOR_E2E_BAR_DEF` (ruta de un `.qxf` de una barra motorizada, para comprobar el barrido de tilt), `DIRECTOR_E2E_RIG=bars` (parchear solo esas barras) y `DIRECTOR_E2E_SAVE` (guardar el proyecto generado).

En Windows se probó con la compilación de CI del fork (`5418f4f`): el wizard y el panel a mano, la demostración en las vistas 2D y 3D, y una sesión de una hora con el emisor VertexAV real (Mixxx), en la que el Director recibió beats, BPM, sección e intensidad y cambió de look en el compás. El barrido de tilt se comprobó en la vista DMX con la definición del SteelPro 1030.

No se ha probado: macOS, la salida DMX con equipos conectados, ni una canción que el emisor divida en varias secciones.

### Traza

Si la variable de entorno `QLCPLUS_DIRECTOR_LOG` contiene la ruta de un archivo, el Director escribe en él cada conexión y pérdida de la entrada, los cambios de sección e intensidad, un resumen cada 16 beats y cada look que arranca. Sirve para verificar una sesión sin mirar la pantalla.

## Cambio en el Show Wizard

Además de rellenar el manifiesto, se corrigió un defecto del wizard: los chasers «Color Rainbow», «Split Color» y «Strobe Chase» se construían pero nunca se añadían al proyecto, así que solo quedaban sus escenas sueltas. Ahora se añaden, y por eso el wizard genera tres funciones más por grupo que antes.

## Código

| Archivo | Qué hace |
| --- | --- |
| `qmlui/director/directorengine.{h,cpp}` | Núcleo de decisión. No depende de la interfaz ni del motor: recibe estado musical y reglas, devuelve qué arrancar y qué parar |
| `qmlui/director/directormanager.{h,cpp}` | Lee los canales de VertexAV, ejecuta las decisiones sobre `Doc`, modo demostración, reglas por defecto y guardado |
| `qmlui/director/directormanifest.h` | Lo que el wizard le cuenta al Director sobre lo que generó |
| `qmlui/qml/director/DirectorPanel.qml` | Panel flotante |
| `qmlui/stagewizard/stagewizard.{h,cpp}` | Rellena el manifiesto y entrega el show al Director |
| `qmlui/director/test/` | Pruebas del núcleo, del gestor y de extremo a extremo, herramienta de captura del panel, `run-tests.sh` y `Dockerfile` |
