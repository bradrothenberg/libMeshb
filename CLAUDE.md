# CLAUDE.md - Project Guidelines for libMeshb

## Build System

- **Platform**: Windows with Visual Studio Build Tools 2022
- **Build command**: Use MSBuild directly since CMake may not be in PATH
  ```
  "/c/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/MSBuild/Current/Bin/MSBuild.exe" <project>.vcxproj -p:Configuration=Release -p:Platform=x64
  ```
- **Build directory**: `build/` contains Visual Studio solution and project files
- **Utilities location**: `build/utilities/Release/*.exe`

## Windows Compatibility

When writing C code for Windows:
- Use `_stricmp` instead of `strcasecmp` (wrap with `#ifdef _WIN32`)
- Use `_strnicmp` instead of `strncasecmp`
- Avoid `strings.h` - it's POSIX only, use `string.h`
- Large stack allocations (>1MB) will cause silent crashes - use heap allocation with `calloc`/`malloc`

## Testing Executables on Windows

PowerShell output capture works better than cmd.exe for testing:
```powershell
powershell.exe -NoProfile -Command "Start-Process -FilePath 'path\to\exe' -ArgumentList 'arg1','arg2' -NoNewWindow -Wait -RedirectStandardOutput 'stdout.txt' -RedirectStandardError 'stderr.txt'; Get-Content 'stdout.txt'"
```

## Git Workflow

- **Remote**: Origin points to upstream (LoicMarechal/libMeshb)
- **Push to fork**: Use explicit URL: `git push https://github.com/bradrothenberg/libMeshb.git <branch>`
- **Branch**: `ntop` for ntop-related development

## Mesh Format Utilities

Created utilities in `utilities/` folder:
- `stl2mesh` - STL (binary/ASCII) to meshb converter with vertex deduplication
- `bdf2mesh` - Nastran BDF to meshb (supports GRID/GRID*, all element types)
- `mesh2stl` - meshb to STL (binary/ASCII output)
- `mesh2bdf` - meshb to Nastran BDF (large/small field format)
- `combinemesh` - Merge BDF volume mesh + STL surface mesh into single meshb

## Nastran BDF Format Notes

- GRID (small field): 8-character fields starting at column 8
- GRID* (large field): 16-character fields, spans two lines with continuation
- Implicit exponent format: "1.5-3" means 1.5e-3 (no 'E' character)
- Element types: CTRIA3, CQUAD4, CTETRA, CPENTA, CHEXA, CPYRAM

## File Locations

- **Sample meshes**: `sample_meshes/`
- **Built executables**: `build/utilities/Release/`
- **Copy utilities to**: `../refine/util/` for use with refine project
