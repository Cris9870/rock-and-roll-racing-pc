# Rock & Roll Racing — PC (panorámico, Windows)

Port nativo para Windows de **Rock & Roll Racing** / **Rock n' Roll Racing** (SNES, 1993), con
pantalla panorámica real hasta 21:9, filtros de imagen sin dientes de sierra, mando y teclado.
Basado en la recompilación estática [snesrecomp](https://github.com/mstan/snesrecomp) y en el port
[fulalas/RockNRollRacingSNESRecomp](https://github.com/fulalas/RockNRollRacingSNESRecomp).

> **Necesitas tu propia copia del juego.** Este proyecto no incluye el juego ni nada sacado de él:
> ni la ROM, ni gráficos, ni código traducido. Cada jugador aporta su ROM y el programa trabaja
> con ella en su propio equipo.

*English summary below.*

## Jugar (sin conocimientos técnicos)

1. Descarga el `.zip` de la última versión en **[Releases](../../releases)** y descomprímelo en una
   carpeta normal (Escritorio o Documentos; evita "Archivos de programa").
2. Abre `RockNRollRacingSNESRecomp.exe`.
3. Pulsa **Elegir archivo…** (o arrastra tu ROM a la ventana) y luego **JUGAR**.

La primera vez el lanzador guarda una copia de tu ROM en la carpeta `rom\` y prepara su aspecto a
partir de ella (unos segundos). La carpeta es portátil: puedes moverla o copiarla a otro PC.

| ROM | Estado |
|---|---|
| Rock & Roll Racing (Europa) | Probada de principio a fin |
| Rock n' Roll Racing (USA) | Compatible, **sin verificar todavía**; si la pruebas, cuéntalo en *Issues* |

Se aceptan archivos `.sfc` y `.smc`, con o sin cabecera de copiador.

## Qué incluye

- **Panorámico real** en carrera (16:9, 21:9 o ajustado al monitor): se ve más pista, no se estira la
  imagen. Funciona también a pantalla partida. Los menús se muestran en 4:3.
- **Filtros**: Suavizada (Scale2x + anti-aliasing: sin dientes de sierra), Nítida, CRT y TV clásica.
- **Movimiento suave**: interpola las ~20 imágenes/s del juego hasta la frecuencia de tu monitor.
- **Mando** (Xbox, PlayStation…) y **teclado** (WASD + J I K L, Q/O).
- **Partidas guardadas** en cualquier momento y **rebobinado**.
- Lanzador en español con el estilo del juego.

### Controles

| Función | Mando (Xbox) | Teclado |
|---|---|---|
| Dirección | Cruceta | W A S D |
| Botones SNES B · A | A · B | K · L |
| Botones SNES Y · X | X · Y | J · I |
| L · R | LB · RB | Q · O |
| Start · Select | Start · Select (View) | Enter · Mayús derecha |
| Menú de partidas | Select (View) + RB | F11 |
| Rebobinar | Select (View) + R3 | F12 |
| Carga / guardado rápido | (en el menú) | F1–F10 / Mayús+F1–F10 |

## Compilar desde el código

Requisitos (Windows): [llvm-mingw](https://github.com/mstorsjo/llvm-mingw), CMake, Ninja, Python 3 y
Git Bash.

```sh
git clone --recursive https://github.com/Cris9870/rock-and-roll-racing-pc
cd rock-and-roll-racing-pc
cmake -S . -B build-public -G Ninja -DCMAKE_BUILD_TYPE=Release -DRNRR_PUBLIC_BUILD=ON \
  -DCMAKE_C_COMPILER=x86_64-w64-mingw32-clang -DCMAKE_CXX_COMPILER=x86_64-w64-mingw32-clang++ \
  -DCMAKE_RC_COMPILER=x86_64-w64-mingw32-windres \
  -DSNESRECOMP_STATIC_RUNTIME=ON -DSNESRECOMP_SDL_BACKEND=SDL3 -DSNESRECOMP_FRAME_IMPL=LLE
cmake --build build-public --target RockNRollRacingSNESRecomp
bash tools/package_public.sh build-public      # crea dist/*.zip
```

`RNRR_PUBLIC_BUILD=ON` produce el ejecutable distribuible: no lleva nada derivado de una ROM y
ejecuta el juego con el intérprete del framework.

**Motor nativo (opcional, solo para uso propio).** Con la ROM europea puedes generar el código C
recompilado (`bash tools/regen.sh --rom <tu ROM>`, necesita Rust 1.97.1 `gnullvm`) y compilar sin
`RNRR_PUBLIC_BUILD`; el lanzador mostrará entonces la opción *Motor: Nativo*. Ese ejecutable contiene
código traducido de tu ROM: **no lo distribuyas.**

## Créditos y licencias

- Port original: [fulalas/RockNRollRacingSNESRecomp](https://github.com/fulalas/RockNRollRacingSNESRecomp).
- Framework: [snesrecomp](https://github.com/mstan/snesrecomp) de Matthew Stanley.
- Este proyecto: adaptación a la revisión europea y USA, panorámico hasta 21:9, filtros, lanzador,
  controles y empaquetado portátil.

El código está bajo **PolyForm Noncommercial 1.0.0** (`LICENSE`): uso y redistribución permitidos
sin fines comerciales. Terceros en `licenses/`: snesrecomp (PolyForm Noncommercial), SDL3 (zlib),
Dear ImGui (MIT) y fuentes Lato (SIL OFL 1.1). *Rock n' Roll Racing* es © Blizzard Entertainment;
este proyecto no está afiliado ni respaldado por sus titulares.

---

## English

A native Windows port of **Rock n' Roll Racing** (SNES) with true widescreen up to 21:9, jaggy-free
image filters, gamepad and keyboard support. **Bring your own ROM**: nothing from the game ships
here. Download the zip from *Releases*, unzip it, run `RockNRollRacingSNESRecomp.exe`, pick (or
drop) your ROM and press **JUGAR**. The European release is fully tested; the USA release is
supported by signature but not yet verified — reports welcome. Non-commercial license
(PolyForm Noncommercial 1.0.0).
