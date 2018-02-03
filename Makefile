CC=gcc
cryptoprice: cryptoprice.c
	gcc -o cryptoprice cryptoprice.c -lcurl -g
