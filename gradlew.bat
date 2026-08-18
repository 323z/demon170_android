@rem Gradle wrapper script for Windows - downloads Gradle if missing
@echo off
setlocal

set DIR=%~dp0
set GRADLE_USER_HOME=%GRADLE_USER_HOME:-=%
if "%GRADLE_USER_HOME%"=="" set GRADLE_USER_HOME=%USERPROFILE%\.gradle

set GRADLE_VERSION=6.7.1
set GRADLE_DIR=%GRADLE_USER_HOME%\wrapper\dists\gradle-%GRADLE_VERSION%-bin
set GRADLE_ZIP=%GRADLE_DIR%\gradle-%GRADLE_VERSION%-bin.zip
set GRADLE_HOME=%GRADLE_DIR%\gradle-%GRADLE_VERSION%

if not exist "%GRADLE_HOME%" (
    echo Downloading Gradle %GRADLE_VERSION%...
    if not exist "%GRADLE_DIR%" mkdir "%GRADLE_DIR%"
    if not exist "%GRADLE_ZIP%" (
        powershell -Command "(New-Object Net.WebClient).DownloadFile('https://mirrors.cloud.tencent.com/gradle/gradle-%GRADLE_VERSION%-bin.zip', '%GRADLE_ZIP%')" 2>nul
        if errorlevel 1 (
            powershell -Command "(New-Object Net.WebClient).DownloadFile('https://services.gradle.org/distributions/gradle-%GRADLE_VERSION%-bin.zip', '%GRADLE_ZIP%')"
        )
    )
    echo Extracting Gradle...
    powershell -Command "Expand-Archive -Path '%GRADLE_ZIP%' -DestinationPath '%GRADLE_DIR%' -Force"
)

set GRADLE_BIN=
for /d %%d in ("%GRADLE_HOME%\*") do (
    if exist "%%d\bin\gradle.bat" set GRADLE_BIN=%%d\bin\gradle.bat
)
if "%GRADLE_BIN%"=="" set GRADLE_BIN=%GRADLE_HOME%\bin\gradle.bat

if not exist "%GRADLE_BIN%" (
    echo ERROR: Cannot find gradle.bat in %GRADLE_HOME%
    dir "%GRADLE_HOME%"
    exit /b 1
)

echo Using Gradle: %GRADLE_BIN%
"%GRADLE_BIN%" %*
