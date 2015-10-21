# CS 241
# University of Illinois

CC = gcc
INC = -I.
FLAGS = -g -W -Wall
LIBS = -lpthread

all: server doc/html

doc/html: server.c libdictionary.c queue.c doc/Doxyfile
	doxygen doc/Doxyfile

server: libdictionary.o queue.o server.c
	$(CC) $(FLAGS) $(INC) $^ -o $@ $(LIBS)

libdictionary.o: libdictionary.c libdictionary.h
	$(CC) -c $(FLAGS) $(INC) $< -o $@ $(LIBS)

queue.o: queue.c queue.h
	$(CC) -c $(FLAGS) $(INC) $< -o $@ $(LIBS)

clean:
	$(RM) *.o server