<nul set /p="#define GIT_COMMIT " > %~dp0\version.h.tmp 
git --work-tree=%~dp0 rev-parse --short HEAD >> %~dp0\version.h.tmp
if %ERRORLEVEL% neq 0 echo #define NO_GIT_COMMIT > %~dp0\version.h.tmp
fc %~dp0\version.h.tmp %~dp0\version.h
if %ERRORLEVEL% neq 0 copy /Y %~dp0\version.h.tmp %~dp0\version.h
del %~dp0\version.h.tmp
