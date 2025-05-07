@echo off
rem let's compress the "sources.tar" file, decompress it again and check file integrity

zbb -p sources.tar
ren sources.tar sources.tar.1
zbb -d sources.tar.zbb
fc /B sources.tar sources.tar.1
del sources.tar.1
del sources.tar.zbb
pause
