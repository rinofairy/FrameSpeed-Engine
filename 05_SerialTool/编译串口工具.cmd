@echo off
cd /d "%~dp0"
"%WINDIR%\Microsoft.NET\Framework64\v4.0.30319\csc.exe" /nologo /codepage:65001 /target:winexe /out:FrameSpeedSerial.exe /reference:System.Windows.Forms.dll /reference:System.Drawing.dll FrameSpeedSerial.cs AsyncSerial.cs SerialToolTests.cs
pause
