# DMW3 PC Edition — project rules

A from-scratch C++17 PC engine for Digimon World 3 (PS1) that reads the player's **own** extracted disc data at runtime.

## Layout
- `src/core` IO + data-root discovery · `src/formats` pure parsers · `src/render` OpenGL renderer · `src/game` game modes · `src/platform` Win32 entry/input · `tools/dw3check.cpp` corpus checker/dumper.
- Game data lives OUTSIDE the repo: `C:\DW3_Project\game_data` (contains `DAT/`, `PRO/`, `STR/`). Found automatically by walking up from the exe, or via `--data` / `DW3_DATA`.

## Build & verify (Windows, VS 2022/2026 with C++ workload)
```
build.bat                      # Release build -> build\dmw3.exe, build\dw3check.exe
build\dw3check.exe             # must print "OK (0 failures)"
build\dmw3.exe --stage S200 --autowalk --frames 240 --screenshot shot.bmp   # headless-ish smoke test
```

## Hard rules
1. **Never commit game data** or anything extracted from it (BIN/PRO/STR, dumped images/audio/text). Only code + format documentation.
2. Every parser change must keep `dw3check` at 0 failures across the full corpus.
3. Document every decoded format in `docs/FORMATS.md` (offsets, confidence, open questions).
4. Push to GitHub only after the qa-release-engineer checklist passes and the owner confirmed the repo.

## Expert agents (`.claude/agents/`)
ps1-reverse-engineer · engine-architect · graphics-programmer · audio-engineer · gameplay-systems-designer · qa-release-engineer.
See `docs/ROADMAP.md` for who owns which milestone.
