set PART=fs
.\\fatdisk.exe
.\\ota_tools.exe -f fs.bin -o fs_%date:~0,4%%date:~5,2%%date:~8,2%.rbl -v %date:~0,4%%date:~5,2%%date:~8,2% -p %PART% -c quicklz
del /f /s /q fs.bin