# Banco di prova su host per src/verify.c.
#
# Compila il codice vero del bootloader con un backend crittografico OpenSSL
# al posto di X-CUBE-CRYPTOLIB, così da poterlo eseguire su PC.

CC      ?= gcc
CFLAGS  ?= -std=c11 -Wall -Wextra -Werror -Wconversion -pedantic -O1 -g
INC     := -I../../inc
LDLIBS  := -lcrypto

SRC := ../../src/verify.c crypto_openssl.c run_vectors.c
ISOTP_SRC := ../../src/isotp.c ../../src/can_dlen.c test_isotp.c

all: run_vectors test_isotp

run_vectors: $(SRC)
	$(CC) $(CFLAGS) $(INC) $(SRC) -o $@ $(LDLIBS)

# Il protocollo non tocca l'hardware, quindi non serve libcrypto.
test_isotp: $(ISOTP_SRC)
	$(CC) $(CFLAGS) $(INC) $(ISOTP_SRC) -o $@

clean:
	rm -f run_vectors test_isotp

.PHONY: all clean
