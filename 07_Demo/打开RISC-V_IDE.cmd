@echo off
set "EFINITY_HOME=F:\Efinity\2026.1"
set "PATH=%EFINITY_HOME%\build_tools\bin;%EFINITY_HOME%\toolchain\bin;%PATH%"
start "" "%EFINITY_HOME%\Efinity-RISCV-IDE\efinity-riscv-ide.exe"
