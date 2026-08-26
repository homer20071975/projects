/*
 * test_isotp.c — collaudo di src/isotp.c su host.
 *
 * Mette due collegamenti ISO-TP uno di fronte all'altro su un bus simulato e
 * verifica che un messaggio consegnato da una parte riemerga identico
 * dall'altra, per lunghezze che attraversano tutte le forme del protocollo:
 * Single Frame classico, Single Frame esteso del CAN FD, First Frame con
 * lunghezza a 12 bit e con lunghezza estesa a 32 bit.
 *
 * Poi i casi che contano davvero, cioè quelli storti: sequenza fuori ordine,
 * timeout in attesa del Flow Control, timeout in attesa dei Consecutive
 * Frame, messaggio più grande del buffer, richiesta di attesa dal ricevente.
 *
 * Il bus simulato non perde frame e non li riordina: qui si collauda la
 * macchina a stati, non la robustezza al rumore elettrico.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "isotp.h"

#define ID_A 0x700u
#define ID_B 0x708u
#define QUEUE_LEN 512
#define BUF_LEN 8192

static int failures = 0;
static int checks = 0;

#define CHECK(cond, ...) do {                                            \
    checks++;                                                            \
    if (!(cond)) {                                                       \
        failures++;                                                      \
        printf("  FALLITO  %s:%d  ", __func__, __LINE__);                \
        printf(__VA_ARGS__);                                             \
        printf("\n");                                                    \
    }                                                                    \
} while (0)

/* --- bus simulato ------------------------------------------------------- */

typedef struct {
    sbl_can_frame_t q[QUEUE_LEN];
    int head, tail;
    int dropped;
} fifo_t;

#define LOG_LEN 4096

typedef struct {
    fifo_t  *out;        /* dove finiscono i frame trasmessi */
    uint32_t *clock;
    int      full_fail;  /* simula code hardware piene */
    /* Istante e primo byte di ogni frame trasmesso: serve a verificare che
     * le separazioni temporali imposte dall'altro capo siano rispettate. */
    uint32_t log_t[LOG_LEN];
    uint8_t  log_pci[LOG_LEN];
    int      log_n;
} endpoint_t;

static void fifo_push(fifo_t *f, const sbl_can_frame_t *frame)
{
    if (((f->tail + 1) % QUEUE_LEN) == f->head) {
        f->dropped++;
        return;
    }
    f->q[f->tail] = *frame;
    f->tail = (f->tail + 1) % QUEUE_LEN;
}

static int fifo_pop(fifo_t *f, sbl_can_frame_t *frame)
{
    if (f->head == f->tail) {
        return 0;
    }
    *frame = f->q[f->head];
    f->head = (f->head + 1) % QUEUE_LEN;
    return 1;
}

static int ep_send(void *ctx, const sbl_can_frame_t *frame)
{
    endpoint_t *ep = (endpoint_t *)ctx;
    if (ep->full_fail > 0) {
        ep->full_fail--;
        return SBL_CAN_BUSY;
    }
    if (ep->log_n < LOG_LEN) {
        ep->log_t[ep->log_n] = *ep->clock;
        ep->log_pci[ep->log_n] = frame->data[0];
        ep->log_n++;
    }
    fifo_push(ep->out, frame);
    return SBL_CAN_OK;
}

static uint32_t ep_now(void *ctx)
{
    return *((endpoint_t *)ctx)->clock;
}

/* --- banco di prova ----------------------------------------------------- */

typedef struct {
    isotp_link_t a, b;
    endpoint_t   ea, eb;
    fifo_t       a_to_b, b_to_a;
    uint32_t     clock;
    uint8_t      buf_a[BUF_LEN], buf_b[BUF_LEN];
} rig_t;

static void rig_init(rig_t *r, uint8_t dlen, uint8_t bs, uint8_t stmin)
{
    isotp_config_t ca, cb;
    isotp_bus_t ba, bb;

    memset(r, 0, sizeof(*r));
    r->ea.out = &r->a_to_b; r->ea.clock = &r->clock;
    r->eb.out = &r->b_to_a; r->eb.clock = &r->clock;

    ca.tx_id = ID_A; ca.rx_id = ID_B;
    ca.tx_dlen = dlen; ca.block_size = bs; ca.st_min = stmin;
    ca.pad_byte = 0xCC; ca.pad_frames = false;
    cb = ca; cb.tx_id = ID_B; cb.rx_id = ID_A;

    ba.send = ep_send; ba.now_ms = ep_now; ba.ctx = &r->ea;
    bb.send = ep_send; bb.now_ms = ep_now; bb.ctx = &r->eb;

    isotp_init(&r->a, &ca, &ba, r->buf_a, BUF_LEN);
    isotp_init(&r->b, &cb, &bb, r->buf_b, BUF_LEN);
}

/* Un giro di bus: consegna i frame in transito e fa avanzare le due parti. */
static void pump(rig_t *r, int rounds, uint32_t ms_per_round)
{
    sbl_can_frame_t f;
    for (int i = 0; i < rounds; i++) {
        while (fifo_pop(&r->a_to_b, &f)) {
            isotp_on_frame(&r->b, &f);
        }
        while (fifo_pop(&r->b_to_a, &f)) {
            isotp_on_frame(&r->a, &f);
        }
        isotp_poll(&r->a);
        isotp_poll(&r->b);
        r->clock += ms_per_round;
    }
}

static void fill(uint8_t *buf, uint32_t len, uint8_t seed)
{
    for (uint32_t i = 0u; i < len; i++) {
        buf[i] = (uint8_t)(seed + (i * 31u) + (i >> 8));
    }
}

/* --- casi ---------------------------------------------------------------- */

static void round_trip(uint32_t len, uint8_t dlen, uint8_t bs, uint8_t stmin)
{
    static uint8_t src[BUF_LEN];
    static rig_t r;
    const uint8_t *got = NULL;
    uint32_t got_len = 0u;

    rig_init(&r, dlen, bs, stmin);
    fill(src, len, (uint8_t)(len & 0xFFu));

    CHECK(isotp_send(&r.a, src, len) == ISOTP_OK
          || isotp_tx_result(&r.a) != ISOTP_ERR_PARAM,
          "invio rifiutato per len=%u", len);

    pump(&r, 4000, 1u);

    CHECK(isotp_take(&r.b, &got, &got_len) == ISOTP_DONE,
          "len=%u dlen=%u bs=%u: messaggio mai completato", len, dlen, bs);
    CHECK(got_len == len, "len=%u dlen=%u: ricevuti %u byte", len, dlen,
          got_len);
    if (got && (got_len == len)) {
        CHECK(memcmp(got, src, len) == 0,
              "len=%u dlen=%u: contenuto diverso", len, dlen);
    }
    CHECK(r.a_to_b.dropped == 0 && r.b_to_a.dropped == 0,
          "len=%u: la coda simulata ha perso frame", len);
}

static void test_round_trip_classico(void)
{
    /* CAN classico: 8 byte per frame. */
    const uint32_t lens[] = { 1u, 2u, 6u, 7u, 8u, 9u, 13u, 100u, 1000u,
                              4094u, 4095u, 4096u, 5000u };
    for (size_t i = 0u; i < sizeof(lens) / sizeof(lens[0]); i++) {
        round_trip(lens[i], 8u, 0u, 0u);
    }
}

static void test_round_trip_can_fd(void)
{
    const uint32_t lens[] = { 1u, 7u, 8u, 61u, 62u, 63u, 64u, 200u, 4095u,
                              4096u, 8000u };
    for (size_t i = 0u; i < sizeof(lens) / sizeof(lens[0]); i++) {
        round_trip(lens[i], 64u, 0u, 0u);
    }
}

static void test_block_size(void)
{
    /* Con BS diverso da zero il mittente deve fermarsi e attendere un FC. */
    const uint8_t bss[] = { 1u, 2u, 4u, 16u };
    for (size_t i = 0u; i < sizeof(bss) / sizeof(bss[0]); i++) {
        round_trip(3000u, 8u, bss[i], 0u);
        round_trip(3000u, 64u, bss[i], 0u);
    }
}

static void test_st_min(void)
{
    /* STmin in millisecondi e nella forma sotto il millisecondo. */
    round_trip(500u, 8u, 0u, 5u);
    round_trip(500u, 8u, 0u, 0xF1u);
    round_trip(500u, 64u, 4u, 2u);
}

/*
 * Che il messaggio arrivi non dimostra nulla sul rispetto di STmin: arriva
 * anche se il mittente spara i frame uno dietro l'altro. Qui si misurano le
 * distanze effettive fra Consecutive Frame consecutivi.
 *
 * Questo test è nato da una mutazione: azzerando il calcolo di
 * tx_next_frame l'intera suite restava verde.
 */
static void test_st_min_rispettato(void)
{
    static rig_t r;
    static uint8_t src[400];
    const uint8_t st_min_ms = 5u;
    int gaps = 0;
    uint32_t prev = 0u;
    int have_prev = 0;

    rig_init(&r, 8u, 0u, st_min_ms);
    fill(src, sizeof(src), 0x66u);
    isotp_send(&r.a, src, sizeof(src));
    pump(&r, 4000, 1u);

    for (int i = 0; i < r.ea.log_n; i++) {
        if ((r.ea.log_pci[i] & 0x30u) != 0x20u) {
            continue;   /* solo i Consecutive Frame */
        }
        if (have_prev) {
            uint32_t gap = r.ea.log_t[i] - prev;
            CHECK(gap >= st_min_ms,
                  "separazione di %u ms fra Consecutive Frame, minimo %u",
                  gap, st_min_ms);
            gaps++;
        }
        prev = r.ea.log_t[i];
        have_prev = 1;
    }
    CHECK(gaps > 10, "solo %d intervalli misurati: il test non prova nulla",
          gaps);
}

/* Con STmin a zero i frame devono invece succedersi senza attese. */
static void test_st_min_zero_non_rallenta(void)
{
    static rig_t r;
    static uint8_t src[400];
    int cf = 0;

    rig_init(&r, 8u, 0u, 0u);
    fill(src, sizeof(src), 0x77u);
    isotp_send(&r.a, src, sizeof(src));
    pump(&r, 4000, 1u);

    for (int i = 0; i < r.ea.log_n; i++) {
        if ((r.ea.log_pci[i] & 0x30u) == 0x20u) {
            cf++;
        }
    }
    CHECK(cf > 10, "attesi molti Consecutive Frame, contati %d", cf);
    CHECK(r.ea.log_n > 0 && r.ea.log_t[r.ea.log_n - 1] < 100u,
          "con STmin a zero il messaggio ha impiegato %u ms",
          r.ea.log_n ? r.ea.log_t[r.ea.log_n - 1] : 0u);
}

static void test_frame_di_un_altro_id_ignorato(void)
{
    static rig_t r;
    sbl_can_frame_t f;
    const uint8_t *got;
    uint32_t got_len;

    rig_init(&r, 8u, 0u, 0u);
    memset(&f, 0, sizeof(f));
    f.id = 0x123u;            /* né ID_A né ID_B */
    f.len = 8u;
    f.data[0] = 0x07u;

    CHECK(isotp_on_frame(&r.b, &f) == ISOTP_IDLE,
          "un frame con identificatore estraneo non va interpretato");
    CHECK(isotp_take(&r.b, &got, &got_len) != ISOTP_DONE,
          "un frame estraneo non deve produrre un messaggio");
}

static void test_sequenza_fuori_ordine(void)
{
    static rig_t r;
    static uint8_t src[600];
    sbl_can_frame_t f;

    rig_init(&r, 8u, 0u, 0u);
    fill(src, sizeof(src), 0x11u);
    isotp_send(&r.a, src, sizeof(src));

    /* First Frame e Flow Control passano regolarmente. */
    pump(&r, 2, 1u);

    /* Poi si inietta un Consecutive con numero di sequenza sbagliato. */
    memset(&f, 0, sizeof(f));
    f.id = ID_A;
    f.len = 8u;
    f.data[0] = 0x2Fu;   /* atteso 0x21 */

    CHECK(isotp_on_frame(&r.b, &f) == ISOTP_ERR_SEQUENCE,
          "un numero di sequenza fuori ordine deve essere respinto");
}

static void test_timeout_attesa_flow_control(void)
{
    static rig_t r;
    static uint8_t src[600];

    rig_init(&r, 8u, 0u, 0u);
    fill(src, sizeof(src), 0x22u);
    isotp_send(&r.a, src, sizeof(src));

    /* Il First Frame parte, ma nessuno risponde: si lascia scorrere il tempo
     * senza consegnare frame alla controparte. */
    r.clock += ISOTP_N_BS_MS + 1u;
    CHECK(isotp_poll(&r.a) == ISOTP_ERR_TIMEOUT,
          "l'attesa del Flow Control deve scadere dopo N_Bs");
    CHECK(isotp_tx_state(&r.a) == ISOTP_TX_IDLE,
          "dopo il timeout la trasmissione deve tornare a riposo");
}

static void test_timeout_attesa_consecutive(void)
{
    static rig_t r;
    static uint8_t src[600];

    rig_init(&r, 8u, 0u, 0u);
    fill(src, sizeof(src), 0x33u);
    isotp_send(&r.a, src, sizeof(src));
    pump(&r, 2, 1u);   /* First Frame consegnato, ricezione avviata */

    /* Il mittente ammutolisce a metà messaggio. */
    r.clock += ISOTP_N_CR_MS + 1u;
    CHECK(isotp_poll(&r.b) == ISOTP_ERR_TIMEOUT,
          "l'attesa dei Consecutive Frame deve scadere dopo N_Cr");
}

static void test_messaggio_piu_grande_del_buffer(void)
{
    static rig_t r;
    static uint8_t src[BUF_LEN];
    isotp_config_t cb;
    isotp_bus_t bb;
    static uint8_t piccolo[64];

    rig_init(&r, 8u, 0u, 0u);

    /* Si rimpicciolisce il buffer del ricevente. */
    cb.tx_id = ID_B; cb.rx_id = ID_A; cb.tx_dlen = 8u;
    cb.block_size = 0u; cb.st_min = 0u; cb.pad_byte = 0xCC;
    cb.pad_frames = false;
    bb.send = ep_send; bb.now_ms = ep_now; bb.ctx = &r.eb;
    isotp_init(&r.b, &cb, &bb, piccolo, sizeof(piccolo));

    fill(src, 4000u, 0x44u);
    isotp_send(&r.a, src, 4000u);
    pump(&r, 20, 1u);

    CHECK(isotp_tx_result(&r.a) == ISOTP_ERR_ABORTED,
          "un Flow Control di Overflow deve interrompere l'invio");
    CHECK(isotp_tx_state(&r.a) == ISOTP_TX_IDLE,
          "dopo l'interruzione la trasmissione deve tornare a riposo");
}

static void test_coda_hardware_piena(void)
{
    /*
     * Se il controllore rifiuta un frame perché le code sono piene, il
     * protocollo non deve perdere il pezzo: deve riprovare al giro dopo.
     */
    static rig_t r;
    static uint8_t src[400];
    const uint8_t *got = NULL;
    uint32_t got_len = 0u;

    rig_init(&r, 8u, 0u, 0u);
    fill(src, sizeof(src), 0x55u);
    isotp_send(&r.a, src, sizeof(src));
    pump(&r, 2, 1u);

    r.ea.full_fail = 5;   /* i prossimi cinque invii falliscono */
    pump(&r, 2000, 1u);

    CHECK(isotp_take(&r.b, &got, &got_len) == ISOTP_DONE,
          "il messaggio deve arrivare comunque dopo code piene");
    CHECK(got_len == sizeof(src), "ricevuti %u byte invece di %zu",
          got_len, sizeof(src));
    if (got && got_len == sizeof(src)) {
        CHECK(memcmp(got, src, sizeof(src)) == 0,
              "contenuto alterato dopo code piene");
    }
}

static void test_argomenti_non_validi(void)
{
    static rig_t r;
    static uint8_t src[16];
    isotp_config_t cfg;
    isotp_bus_t bus;
    isotp_link_t link;
    uint8_t buf[32];

    rig_init(&r, 8u, 0u, 0u);
    CHECK(isotp_send(&r.a, NULL, 10u) == ISOTP_ERR_PARAM,
          "un puntatore nullo va respinto");
    CHECK(isotp_send(&r.a, src, 0u) == ISOTP_ERR_PARAM,
          "un messaggio di lunghezza zero va respinto");

    cfg.tx_id = ID_A; cfg.rx_id = ID_B; cfg.block_size = 0u;
    cfg.st_min = 0u; cfg.pad_byte = 0u; cfg.pad_frames = false;
    bus.send = ep_send; bus.now_ms = ep_now; bus.ctx = &r.ea;

    cfg.tx_dlen = 7u;
    CHECK(isotp_init(&link, &cfg, &bus, buf, sizeof(buf)) == ISOTP_ERR_PARAM,
          "meno di 8 byte per frame non è ammesso");
    cfg.tx_dlen = 65u;
    CHECK(isotp_init(&link, &cfg, &bus, buf, sizeof(buf)) == ISOTP_ERR_PARAM,
          "più di 64 byte per frame non è ammesso");
    cfg.tx_dlen = 8u;
    CHECK(isotp_init(&link, &cfg, &bus, NULL, 32u) == ISOTP_ERR_PARAM,
          "serve un buffer di ricezione");
}

static void test_lunghezze_can_fd(void)
{
    CHECK(sbl_can_round_dlen(0u) == 0u, "0");
    CHECK(sbl_can_round_dlen(8u) == 8u, "8");
    CHECK(sbl_can_round_dlen(9u) == 12u, "9 deve salire a 12");
    CHECK(sbl_can_round_dlen(12u) == 12u, "12");
    CHECK(sbl_can_round_dlen(13u) == 16u, "13 deve salire a 16");
    CHECK(sbl_can_round_dlen(25u) == 32u, "25 deve salire a 32");
    CHECK(sbl_can_round_dlen(33u) == 48u, "33 deve salire a 48");
    CHECK(sbl_can_round_dlen(49u) == 64u, "49 deve salire a 64");
    CHECK(sbl_can_round_dlen(64u) == 64u, "64");
}

static void test_riempimento_frame(void)
{
    /* Con pad_frames i frame devono uscire sempre alla lunghezza piena. */
    static rig_t r;
    static uint8_t src[3];
    sbl_can_frame_t f;

    rig_init(&r, 8u, 0u, 0u);
    r.a.cfg.pad_frames = true;
    r.a.cfg.pad_byte = 0xAAu;

    isotp_send(&r.a, src, sizeof(src));
    CHECK(fifo_pop(&r.a_to_b, &f) == 1, "nessun frame trasmesso");
    CHECK(f.len == 8u, "frame lungo %u invece di 8", f.len);
    CHECK(f.data[7] == 0xAAu, "riempimento sbagliato: 0x%02X", f.data[7]);
}

int main(void)
{
    printf("collaudo ISO-TP\n");
    test_lunghezze_can_fd();
    test_round_trip_classico();
    test_round_trip_can_fd();
    test_block_size();
    test_st_min();
    test_st_min_rispettato();
    test_st_min_zero_non_rallenta();
    test_frame_di_un_altro_id_ignorato();
    test_sequenza_fuori_ordine();
    test_timeout_attesa_flow_control();
    test_timeout_attesa_consecutive();
    test_messaggio_piu_grande_del_buffer();
    test_coda_hardware_piena();
    test_argomenti_non_validi();
    test_riempimento_frame();

    printf("isotp: %d verifiche, %d fallite\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
