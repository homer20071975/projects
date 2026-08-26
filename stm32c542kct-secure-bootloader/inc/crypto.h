/*
 * crypto.h — primitive crittografiche, dietro un'interfaccia sottile.
 *
 * Sul target l'implementazione è X-CUBE-CRYPTOLIB con lo SHA-256 appoggiato
 * all'acceleratore HASH (§3 di docs/00-decisions.md). Il C542 non ha il PKA,
 * quindi l'ECDSA è software.
 *
 * L'interfaccia esiste per un motivo preciso: la cryptolib ST non compila su
 * PC, quindi senza questa separazione il codice di verifica non sarebbe
 * testabile fuori dal target — ed è esattamente il codice che non vuoi che
 * abbia bug. Con l'interfaccia, i test su host montano un backend diverso e
 * verificano la stessa logica. Vedi la nota sui test in docs/00-decisions.md.
 */

#ifndef SBL_CRYPTO_H
#define SBL_CRYPTO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SBL_CRYPTO_OK      0
#define SBL_CRYPTO_FAIL   (-1)

/*
 * SHA-256 in un colpo solo.
 *
 * Sul target `data` punta alla flash mappata in memoria, quindi anche i ~100
 * KB del payload si passano direttamente senza copie.
 *
 * Restituisce SBL_CRYPTO_OK o SBL_CRYPTO_FAIL.
 */
int sbl_sha256(const uint8_t *data, size_t len, uint8_t out[32]);

/*
 * Inizializza il backend. Da chiamare una volta prima di ogni altra funzione
 * di questo header.
 *
 * Sul target accende l'acceleratore HASH e inizializza la cryptolib. Il
 * backend di test su host non ne ha bisogno e restituisce SBL_CRYPTO_OK
 * senza fare nulla.
 */
int sbl_crypto_init(void);

/*
 * Verifica ECDSA su curva P-256 con digest SHA-256.
 *
 *   pubkey     X || Y, 64 byte, coordinate non compresse
 *   msg, len   messaggio in chiaro: l'implementazione ne calcola il digest
 *   signature  r || s, 64 byte
 *
 * Restituisce true solo se la firma è valida. Non solleva né segnala altri
 * esiti: qualunque anomalia è un fallimento.
 */
bool sbl_ecdsa_p256_verify(const uint8_t pubkey[64],
                           const uint8_t *msg, size_t len,
                           const uint8_t signature[64]);

#endif /* SBL_CRYPTO_H */
