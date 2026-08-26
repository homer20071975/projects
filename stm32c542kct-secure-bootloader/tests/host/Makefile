# Banco di prova su host per src/verify.c.
#
# Compila il codice vero del bootloader con un backend crittografico OpenSSL
# al posto di X-CUBE-CRYPTOLIB, così da poterlo eseguire su PC.

CC      ?= gcc
CFLAGS  ?= -std=c11 -Wall -Wextra -Werror -Wconversion -pedantic -O1 -g
INC     := -I../../inc
LDLIBS  := -lcrypto

SRC := ../../src/verify.c crypto_openssl.c run_vectors.c

run_vectors: $(SRC)
	$(CC) $(CFLAGS) $(INC) $(SRC) -o $@ $(LDLIBS)

clean:
	rm -f run_vectors

.PHONY: clean
