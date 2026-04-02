#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "hem/hem.h"

static void sep(void)
{
    printf("----------------------------------------------------\n");
}

static int check(hem_ctx_t *ctx, hem_error_t err, const char *step)
{
    if (err == HEM_OK) {
        printf("[PASS] %s\n", step);
        return 1;
    }
    printf("[FAIL] %s\n"
           "       error : %s\n"
           "       detail: %s\n"
           "       HTTP  : %d\n",
           step,
           hem_error_string(err),
           hem_last_error_msg(ctx),
           hem_last_http_status(ctx));
    return 0;
}

static void print_version(const hem_version_t *v)
{
    printf("       Hardware  : %s\n", v->hwv);
    printf("       Bootloader: %s\n", v->blv);
    printf("       Firmware  : %s\n", v->fwv);
}

static void print_status(const hem_status_t *s)
{
    printf("       Hostname   : %s\n",  s->hostname[0] ? s->hostname : "(not set)");
    printf("       Initialized: %s\n",  s->initialized ? "yes" : "no");
    printf("       HTTPS      : %s\n",  s->https ? "yes" : "no");
    printf("       FLS state  : %d\n",  s->fls_state);
    printf("       Uptime     : %ds\n", s->uptime);
    printf("       Temp       : %d°C\n", s->temp);
    if (s->ts >= 0)
        printf("       RTC ts     : %lld\n", (long long)s->ts);
    else
        printf("       RTC ts     : not set\n");
}

static void print_hex(const uint8_t *data, size_t len)
{
    for (size_t i = 0; i < len && i < 16; i++)
        printf("%02x", data[i]);
    if (len > 16) printf("...");
}

int main(int argc, char *argv[])
{
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <device_url> <passphrase>\n", argv[0]);
        fprintf(stderr, "  device_url  e.g. https://my.ence.do\n");
        return 1;
    }

    const char *url        = argv[1];
    const char *passphrase = argv[2];

    srand((unsigned)time(NULL));

    printf("\nEncedo HEM Client -- MVP Test\n");
    printf("Device: %s\n", url);
    sep();

    hem_ctx_t *ctx = hem_ctx_create(url);
    if (!ctx) { fprintf(stderr, "Failed to create context\n"); return 1; }
    hem_ctx_set_credentials(ctx, passphrase, HEM_ROLE_USER);

    int failures = 0;

    /* ------------------------------------------------------------------ */
    printf("\n[1] Device version\n");
    hem_version_t ver = {0};
    if (check(ctx, hem_system_version(ctx, &ver), "GET /api/system/version"))
        print_version(&ver);
    else failures++;

    /* ------------------------------------------------------------------ */
    printf("\n[2] Device status\n");
    hem_status_t status = {0};
    if (check(ctx, hem_system_status(ctx, &status), "GET /api/system/status"))
        print_status(&status);
    else failures++;

    /* ------------------------------------------------------------------ */
    printf("\n[3] Check-in\n");
    if (check(ctx, hem_system_checkin(ctx),
              "GET+POST /api/system/checkin (via api.encedo.com)")) {
        printf("       RTC clock set, firmware integrity verified\n");
    } else {
        failures++;
        printf("       (continuing -- check-in failure is non-fatal for crypto steps)\n");
    }

    /* ------------------------------------------------------------------ */
    printf("\n[4] Authenticate + read config\n");
    hem_config_t cfg = {0};
    if (check(ctx, hem_system_config(ctx, &cfg),
              "GET /api/system/config (auto-auth scope: system:config)")) {
        printf("       User     : %s\n", cfg.user[0]     ? cfg.user     : "(not set)");
        printf("       Hostname : %s\n", cfg.hostname[0] ? cfg.hostname : "(not set)");
        printf("       EID      : %.20s...\n", cfg.eid);
    } else failures++;

    /* ------------------------------------------------------------------ */
    printf("\n[5] Create AES256 key\n");
    char kid[33] = {0};
    if (check(ctx, hem_key_create(ctx, "mvp-test-key", "AES256", kid, sizeof(kid)),
              "POST /api/keymgmt/create (scope: keymgmt:gen)")) {
        printf("       KID: %s\n", kid);
    } else {
        failures++;
        /* Cannot continue without a key */
        goto done;
    }

    /* ------------------------------------------------------------------ */
    printf("\n[6] Encrypt random message\n");

    /* Generate a 32-byte random test message */
    uint8_t plaintext[32];
    for (int i = 0; i < 32; i++)
        plaintext[i] = (uint8_t)(rand() & 0xFF);
    printf("       Plaintext : ");
    print_hex(plaintext, sizeof(plaintext));
    printf(" (%zu bytes)\n", sizeof(plaintext));

    uint8_t             ct_buf[64] = {0};
    hem_cipher_result_t enc        = {0};
    char                encrypt_step[96];
    snprintf(encrypt_step, sizeof(encrypt_step),
             "POST /api/crypto/cipher/encrypt (scope: keymgmt:use:%s)", kid);

    if (check(ctx,
              hem_encrypt(ctx, kid, "AES256-GCM",
                          plaintext, sizeof(plaintext),
                          NULL, 0,
                          ct_buf, sizeof(ct_buf),
                          &enc),
              encrypt_step)) {
        printf("       Ciphertext: ");
        print_hex(enc.ciphertext, enc.ciphertext_len);
        printf(" (%zu bytes)\n", enc.ciphertext_len);
        printf("       IV        : ");
        print_hex(enc.iv, enc.iv_len);
        printf(" (%zu bytes)\n", enc.iv_len);
        printf("       Tag       : ");
        print_hex(enc.tag, enc.tag_len);
        printf(" (%zu bytes)\n", enc.tag_len);
    } else {
        failures++;
        goto cleanup_key;
    }

    /* ------------------------------------------------------------------ */
    printf("\n[7] Decrypt and verify\n");

    uint8_t pt_out[64] = {0};
    size_t  pt_len     = 0;
    char    decrypt_step[96];
    snprintf(decrypt_step, sizeof(decrypt_step),
             "POST /api/crypto/cipher/decrypt (scope: keymgmt:use:%s)", kid);

    if (check(ctx,
              hem_decrypt(ctx, kid, "AES256-GCM",
                          enc.ciphertext, enc.ciphertext_len,
                          enc.iv,  enc.iv_len,
                          enc.tag, enc.tag_len,
                          NULL, 0,
                          pt_out, sizeof(pt_out), &pt_len),
              decrypt_step)) {
        printf("       Decrypted : ");
        print_hex(pt_out, pt_len);
        printf(" (%zu bytes)\n", pt_len);

        if (pt_len == sizeof(plaintext) &&
            memcmp(pt_out, plaintext, sizeof(plaintext)) == 0) {
            printf("[PASS] Plaintext matches original\n");
        } else {
            printf("[FAIL] Plaintext does NOT match original!\n");
            failures++;
        }
    } else {
        failures++;
    }

cleanup_key:
    /* ------------------------------------------------------------------ */
    printf("\n[8] Delete test key\n");
    {
        char delete_step[96];
        snprintf(delete_step, sizeof(delete_step),
                 "DELETE /api/keymgmt/delete/%s (scope: keymgmt:del)", kid);
        if (!check(ctx, hem_key_delete(ctx, kid), delete_step))
            failures++;
    }

done:
    sep();
    if (failures == 0)
        printf("ALL STEPS PASSED\n\n");
    else
        printf("%d STEP(S) FAILED\n\n", failures);

    hem_ctx_destroy(ctx);
    return failures > 0 ? 1 : 0;
}
