CC=gcc
CFLAGS=-g
cryptoprice: cryptoprice.c
	gcc -o cryptoprice cryptoprice.c jsmn/jsmn.h jsmn/jsmn.c -lcurl
