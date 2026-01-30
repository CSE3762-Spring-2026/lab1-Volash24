# Makefile for client and server

CC = gcc
OBJCS = client1.c

CFLAGS =  -g -Wall
# setup for system
nLIBS =

all: client1 

client1: $(OBJCS)
	$(CC) $(CFLAGS) -o $@ $(OBJCS) $(LIBS)

clean:
	rm client1 
