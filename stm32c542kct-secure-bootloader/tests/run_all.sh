#!/bin/sh
# Esegue tutti i test. Da eseguire dalla radice del progetto.
set -e
echo "== header C =="
gcc -std=c11 -Wall -Wextra -Werror -fsyntax-only -Iinc -xc - <<'END'
#include "memory_map.h"
#include "verify.h"
#include "crypto.h"
int main(void) { return 0; }
END
echo "ok"
echo
echo "== formato immagine (riferimento Python) =="
python3 tests/test_image_format.py
echo
echo "== confronto differenziale C / Python =="
python3 tests/test_differential.py
