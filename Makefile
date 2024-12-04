# Makefile for WebServerMain.c using MinGW

CC = gcc
CFLAGS = -Wall -O2
LIBS = -lws2_32

all: WebServerMain.exe

WebServerMain.exe: WebServerMain.c
	$(CC) $(CFLAGS) -o WebServerMain.exe WebServerMain.c $(LIBS)

clean:
	del WebServerMain.exe
