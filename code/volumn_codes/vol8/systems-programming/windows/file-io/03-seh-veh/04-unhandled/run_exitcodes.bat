@echo off
rem 读取崩溃退出码的正经姿势:
rem   1. %ERRORLEVEL% 在同一行复合命令里是【解析期】展开,必须开 /v:on 用 !...! 延迟展开
rem   2. WSL 里直接 ./xxx.exe 后看 $?,是 wait status 的低 8 位:0xC0000005 截成 5,读不出全码
cmd /v:on /c "e4_bare.exe < nul    & echo bare    =!ERRORLEVEL!"
cmd /v:on /c "e4_ueh.exe   < nul    & echo ueh     =!ERRORLEVEL!"
cmd /v:on /c "e4_errmode.exe < nul  & echo errmode =!ERRORLEVEL!"
