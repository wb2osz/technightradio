
APPS := timetest1 dds udpsend

all: ${APPS}

CPPFLAGS = -g -Wall


timetest1: timetest1.o
	g++ -o $@ $^

timetest2: timetest2.o
	g++ -o $@ $^

dds: dds.o AD9834.o
	g++ -o $@ $^ -ljsoncpp

udpsend: udpsend.o
	g++ -o $@ $^

clean:
	rm -f ${APPS} *.o
