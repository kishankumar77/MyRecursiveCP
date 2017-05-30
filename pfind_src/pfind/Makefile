CFLAGS = -O2 -g -D_POSIX_PTHREAD_SEMANTICS -lpthread

default:
	/usr/software/bin/g++ $(CFLAGS) -o plink plink.cc -Wl,-rpath -Wl,/usr/software/lib 
	/usr/software/bin/g++ $(CFLAGS) -o pfind pfind.cc -Wl,-rpath -Wl,/usr/software/lib
	/usr/software/bin/g++ $(CFLAGS) -o prmdir prmdir.cc -Wl,-rpath -Wl,/usr/software/lib


bsd:
	/usr/bin/g++ $(CFLAGS) -o pfind pfind.cc
	/usr/bin/g++ $(CFLAGS) -o plink plink.cc
	/usr/bin/g++ $(CFLAGS) -o prmdir prmdir.cc

install:
	ls -l /usr/software/rats
