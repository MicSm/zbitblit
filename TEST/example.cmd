@rem let's compress the "sources.tar" file, decompress it again and check file integrity

zbitblit -p sources.tar
@ren sources.tar sources.tar.1
@zbitblit -d sources.tar.zbb
@fc /B sources.tar sources.tar.1
@del sources.tar.1 
@pause
