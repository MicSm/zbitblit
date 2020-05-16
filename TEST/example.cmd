@rem let's compress the "sources.tar" file, decompress it again and check file integrity

ecp -p sources.tar
@ren sources.tar sources.tar.1
@ecp -d sources.tar.ecp
@fc /B sources.tar sources.tar.1
@del sources.tar.1 
@pause
