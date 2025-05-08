@echo off
rem let's compress the "sources.tar" file, decompress it again and check file integrity

zbb -p -b61 test.tar
ren test.tar test.tar.1
zbb -d test.tar.zbb
fc /B test.tar test.tar.1
del test.tar.1
del test.tar.zbb
pause
