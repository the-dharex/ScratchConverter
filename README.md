# ScratchConverter

Convierte proyectos de Scratch 3 (.sb3) en juegos ejecutables independientes usando SFML.

## Requisitos
- Windows 10/11 (o Linux, experimental)
- CMake 3.20+
- Git
- Visual Studio 2026 (o g++/clang en Linux)
- SFML 2.6.1 (se descarga automáticamente)

## Instalación
1. Clona el repositorio:
   ```sh
   git clone https://github.com/the-dharex/ScratchConverter.git
   cd ScratchConverter
   ```
2. Genera los archivos de build:
   ```sh
   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
   ```
3. Compila:
   ```sh
   cmake --build build --config Release
   ```

## Uso
1. Abre el programa (ScratchConverter.exe en `build/Release/`).
2. Haz clic en el botón para seleccionar un archivo `.sb3` de Scratch.
3. Elige la carpeta de salida.
4. Presiona "Convertir".
5. El juego generado estará en la carpeta seleccionada, listo para ejecutar.

## Notas
- El programa es 100% gráfico, no requiere consola.
- Soporta la mayoría de los bloques de Scratch 3, incluyendo sonidos, clones, listas y procedimientos.
- Los gráficos vectoriales SVG y los textos se rasterizan automáticamente.
- Si encuentras un bug, abre un issue en GitHub.

## Créditos
- Basado en SFML, nanosvg, stb_truetype.
- Autor: the-dharex
