"%WDKToolRoot%\x64\devcon" remove root\vhidmini /force

msbuild vhidmini.sln /p:Configuration=Debug /p:Platform=x64
@if %errorlevel% neq 0 exit /b %errorlevel%

certutil -addstore Root driver\x64\Debug\vhidmini.cer

"%WDKToolRoot%\x64\devcon" install driver\x64\Debug\vhidmini\vhidmini.inf root\vhidmini
@if %errorlevel% neq 0 exit /b %errorlevel%
