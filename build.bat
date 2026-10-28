@echo off
REM ============================================================
REM  Building Rascal on Windows 11 — cc65 + AppleCommander
REM  Place this file in the same folder as all the source files (*.c, *.s,
REM  apple2enh-overlay-10.cfg, hello.bas.txt), and run it by double-clicking
REM  or from the command line.
REM
REM  This version writes the game DIRECTLY onto your existing ProDOS disk
REM  (the one that already has PRODOS and BASIC.SYSTEM on it, with its
REM  volume named RASCAL) — it does NOT create a new disk.
REM
REM  Edit YOURDISK below to match the actual filename of your disk.
REM ============================================================

setlocal enabledelayedexpansion
set STACKSIZE=100
set OVERLAYSIZE=0x1000
set AC_JAR=AppleCommander-ac-13.1.jar
set YOURDISK=rascal.po

echo === Compiling resident C files ===
cl65 -t apple2enh -O -c main.c
if errorlevel 1 goto :error
cl65 -t apple2enh -O -c level.c
if errorlevel 1 goto :error
cl65 -t apple2enh -O -c rng.c
if errorlevel 1 goto :error

echo === Assembling resident .s files ===
for %%f in (rng_asm seen_asm room_asm cell_visible_asm armor_pv_asm effective_defense_asm monster_at_asm hand_weapon_damage_asm item_at_asm effective_strength_asm monster_init_stats_asm weighted_pick_asm ring_worn_effect_asm victory_picture_asm) do (
    ca65 -t apple2enh %%f.s -o %%f.o
    if errorlevel 1 goto :error
)

echo === Compile overlays (each into its own slot) ===
cl65 -t apple2enh -O --code-name OVERLAY1 --rodata-name OVERLAY1 -c menus_view.c
if errorlevel 1 goto :error
cl65 -t apple2enh -O --code-name OVERLAY2 --rodata-name OVERLAY2 -c menus_action1.c
if errorlevel 1 goto :error
cl65 -t apple2enh -O --code-name OVERLAY3 --rodata-name OVERLAY3 -c menus_action2.c
if errorlevel 1 goto :error
cl65 -t apple2enh -O --code-name OVERLAY4 --rodata-name OVERLAY4 -c menus_end.c
if errorlevel 1 goto :error
cl65 -t apple2enh -O --code-name OVERLAY5 --rodata-name OVERLAY5 -c menus_start.c
if errorlevel 1 goto :error
cl65 -t apple2enh -O --code-name OVERLAY6 --rodata-name OVERLAY6 -c spawn.c
if errorlevel 1 goto :error
cl65 -t apple2enh -O --code-name OVERLAY7 --rodata-name OVERLAY7 -c menus_action3.c
if errorlevel 1 goto :error
cl65 -t apple2enh -O --code-name OVERLAY8 --rodata-name OVERLAY8 -c level_shapes.c
if errorlevel 1 goto :error
cl65 -t apple2enh -O --code-name OVERLAY9 --rodata-name OVERLAY9 -c level_connect.c
if errorlevel 1 goto :error
cl65 -t apple2enh -O --code-name OVERLAY10 --rodata-name OVERLAY10 -c rare_ov10.c
if errorlevel 1 goto :error

echo === Link (STACKSIZE=%STACKSIZE%, OVERLAYSIZE=%OVERLAYSIZE%) ===
ld65 -C apple2enh-overlay-10.cfg -D __OVERLAYSIZE__=%OVERLAYSIZE% -D __STACKSIZE__=%STACKSIZE% -o rascal.system -m map.txt ^
    main.o level.o level_shapes.o level_connect.o spawn.o rng.o ^
    rng_asm.o seen_asm.o room_asm.o cell_visible_asm.o armor_pv_asm.o ^
    effective_defense_asm.o monster_at_asm.o hand_weapon_damage_asm.o ^
    item_at_asm.o effective_strength_asm.o monster_init_stats_asm.o ^
    weighted_pick_asm.o ring_worn_effect_asm.o victory_picture_asm.o ^
    menus_view.o menus_action1.o menus_action2.o menus_action3.o ^
    menus_start.o menus_end.o rare_ov10.o apple2enh.lib
if errorlevel 1 goto :error

echo === Compiled without errors. Check map.txt for overflows. ===
findstr /i "overflow" map.txt
if not errorlevel 1 (
    echo WARNING: A slot overflow has been detected; see above!
    pause
)

echo === Renaming volume to RASCAL (the game expects this exact name) ===
java -jar %AC_JAR% -n %YOURDISK% RASCAL
if errorlevel 1 goto :error

echo === Writing RASC onto %YOURDISK% ===
java -jar %AC_JAR% -as %YOURDISK% RASC < rascal.system
if errorlevel 1 goto :error

echo === Writing RASC.1 ===
java -jar %AC_JAR% -p %YOURDISK% RASC.1 bin < rascal.system.1
if errorlevel 1 goto :error

echo === Writing RASC.2 ===
java -jar %AC_JAR% -p %YOURDISK% RASC.2 bin < rascal.system.2
if errorlevel 1 goto :error

echo === Writing RASC.3 ===
java -jar %AC_JAR% -p %YOURDISK% RASC.3 bin < rascal.system.3
if errorlevel 1 goto :error

echo === Writing RASC.4 ===
java -jar %AC_JAR% -p %YOURDISK% RASC.4 bin < rascal.system.4
if errorlevel 1 goto :error

echo === Writing RASC.5 ===
java -jar %AC_JAR% -p %YOURDISK% RASC.5 bin < rascal.system.5
if errorlevel 1 goto :error

echo === Writing RASC.6 ===
java -jar %AC_JAR% -p %YOURDISK% RASC.6 bin < rascal.system.6
if errorlevel 1 goto :error

echo === Writing RASC.7 ===
java -jar %AC_JAR% -p %YOURDISK% RASC.7 bin < rascal.system.7
if errorlevel 1 goto :error

echo === Writing RASC.8 ===
java -jar %AC_JAR% -p %YOURDISK% RASC.8 bin < rascal.system.8
if errorlevel 1 goto :error

echo === Writing RASC.9 ===
java -jar %AC_JAR% -p %YOURDISK% RASC.9 bin < rascal.system.9
if errorlevel 1 goto :error

echo === Writing RASC.10 ===
java -jar %AC_JAR% -p %YOURDISK% RASC.10 bin < rascal.system.10
if errorlevel 1 goto :error

echo === Writing the STARTUP loader (BASIC, autorun) ===
java -jar %AC_JAR% -bas %YOURDISK% STARTUP < hello.bas.txt
if errorlevel 1 goto :error

echo.
echo ========================================
echo   DONE: %YOURDISK%
echo ========================================
goto :end

:error
echo.
echo *** ERROR in one of the steps — see the output above ***
pause
exit /b 1

:end
pause
