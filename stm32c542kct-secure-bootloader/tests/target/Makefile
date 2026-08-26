# Compila il self-test del target sull'host, con il backend OpenSSL.
#
# Serve a dimostrare che il self-test e i vettori generati sono corretti,
# prima ancora di avere il pezzo in mano. Sul target si compilano gli stessi
# file sostituendo crypto_openssl.c con ../../src/crypto_stm32.c.

CC      ?= gcc
CFLAGS  ?= -std=c11 -Wall -Wextra -Werror -Wconversion -pedantic -O1 -g
INC     := -I../../inc -I.
LDLIBS  := -lcrypto

SRC := ../../src/verify.c ../host/crypto_openssl.c vectors_data.c selftest.c

selftest: $(SRC) vectors_data.h
	$(CC) $(CFLAGS) $(INC) $(SRC) -o $@ $(LDLIBS)

vectors_data.c vectors_data.h:
	python3 ../gen_target_vectors.py -o .

regen:
	python3 ../gen_target_vectors.py -o .

clean:
	rm -f selftest

.PHONY: clean regen
