
CC = gcc -O2

all: bin2cpp pbo_read

bin2cpp: bin2cpp.c 

pbo_read: pbo_read.c

clean: 
	rm -f *.o pbo_read bin2cpp
