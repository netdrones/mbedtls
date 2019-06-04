#include "mbedtls/ssl.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/certs.h"
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>


static bool initialized = 0;
#if defined(MBEDTLS_X509_CRT_PARSE_C)
static mbedtls_x509_crt cacert;
#endif
const char *alpn_list[3];


#if defined(MBEDTLS_KEY_EXCHANGE__SOME__PSK_ENABLED)
const unsigned char psk[] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f
};
const char psk_id[] = "Client_identity";
#endif

const char *pers = "fuzz_client";


typedef struct fuzzBufferOffset
{
    const uint8_t *Data;
    size_t Size;
    size_t Offset;
} fuzzBufferOffset_t;

static int dummy_send( void *ctx, const unsigned char *buf, size_t len )
{
    //silence warning about unused parameter
    (void) ctx;
    (void) buf;

    //pretends we wrote everything ok
    return( len );
}

static int fuzz_recv( void *ctx, unsigned char *buf, size_t len )
{
    //reads from the buffer from fuzzer
    fuzzBufferOffset_t * biomemfuzz = (fuzzBufferOffset_t *) ctx;

    if (biomemfuzz->Offset == biomemfuzz->Size) {
        //EOF
        return (0);
    }
    if (len + biomemfuzz->Offset > biomemfuzz->Size) {
        //do not overflow
        len = biomemfuzz->Size - biomemfuzz->Offset;
    }
    memcpy(buf, biomemfuzz->Data + biomemfuzz->Offset, len);
    biomemfuzz->Offset += len;
    return( len );
}

static int dummy_random( void *p_rng, unsigned char *output, size_t output_len )
{
    int ret;
    size_t i;

    //use mbedtls_ctr_drbg_random to find bugs in it
    ret = mbedtls_ctr_drbg_random(p_rng, output, output_len);
    for (i=0; i<output_len; i++) {
        //replace result with pseudo random
        output[i] = (unsigned char) rand();
    }
    return( ret );
}

static int dummy_entropy( void *data, unsigned char *output, size_t len )
{
    size_t i;
    (void) data;

    //use mbedtls_entropy_func to find bugs in it
    //test performance impact of entropy
    //ret = mbedtls_entropy_func(data, output, len);
    for (i=0; i<len; i++) {
        //replace result with pseudo random
        output[i] = (unsigned char) rand();
    }
    return( 0 );
}


int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
    int ret;
    size_t len;
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config conf;
    mbedtls_ctr_drbg_context ctr_drbg;
    mbedtls_entropy_context entropy;
    unsigned char buf[4096];
    fuzzBufferOffset_t biomemfuzz;
    uint16_t options;

    if (initialized == 0) {
#if defined(MBEDTLS_X509_CRT_PARSE_C)
        mbedtls_x509_crt_init( &cacert );
        if (mbedtls_x509_crt_parse( &cacert, (const unsigned char *) mbedtls_test_cas_pem,
                                   mbedtls_test_cas_pem_len ) != 0)
            return 1;
#endif

        alpn_list[0] = "HTTP";
        alpn_list[1] = "fuzzalpn";
        alpn_list[2] = NULL;

        initialized = 1;
    }

    //we take 1 byte as options input
    if (Size < 2) {
        return 0;
    }
    options = (Data[Size - 2] << 8) | Data[Size - 1];
    //Avoid warnings if compile options imply no options
    (void) options;

    mbedtls_ssl_init( &ssl );
    mbedtls_ssl_config_init( &conf );
    mbedtls_ctr_drbg_init( &ctr_drbg );
    mbedtls_entropy_init( &entropy );

    if( mbedtls_ctr_drbg_seed( &ctr_drbg, dummy_entropy, &entropy,
                              (const unsigned char *) pers, strlen( pers ) ) != 0 )
        goto exit;

    if( mbedtls_ssl_config_defaults( &conf,
                                    MBEDTLS_SSL_IS_CLIENT,
                                    MBEDTLS_SSL_TRANSPORT_STREAM,
                                    MBEDTLS_SSL_PRESET_DEFAULT ) != 0 )
        goto exit;

#if defined(MBEDTLS_KEY_EXCHANGE__SOME__PSK_ENABLED)
    if (options & 2) {
        mbedtls_ssl_conf_psk( &conf, psk, sizeof( psk ),
                             (const unsigned char *) psk_id, sizeof( psk_id ) - 1 );
    }
#endif

#if defined(MBEDTLS_X509_CRT_PARSE_C)
    if (options & 4) {
        mbedtls_ssl_conf_ca_chain( &conf, &cacert, NULL );
        mbedtls_ssl_conf_authmode( &conf, MBEDTLS_SSL_VERIFY_REQUIRED );
    } else
#endif
    {
        mbedtls_ssl_conf_authmode( &conf, MBEDTLS_SSL_VERIFY_NONE );
    }
#if defined(MBEDTLS_SSL_TRUNCATED_HMAC)
    mbedtls_ssl_conf_truncated_hmac( &conf, (options & 8) ? MBEDTLS_SSL_TRUNC_HMAC_ENABLED : MBEDTLS_SSL_TRUNC_HMAC_DISABLED);
#endif
#if defined(MBEDTLS_SSL_EXTENDED_MASTER_SECRET)
    mbedtls_ssl_conf_extended_master_secret( &conf, (options & 0x10) ? MBEDTLS_SSL_EXTENDED_MS_DISABLED : MBEDTLS_SSL_EXTENDED_MS_ENABLED);
#endif
#if defined(MBEDTLS_SSL_ENCRYPT_THEN_MAC)
    mbedtls_ssl_conf_encrypt_then_mac( &conf, (options & 0x20) ? MBEDTLS_SSL_ETM_DISABLED : MBEDTLS_SSL_ETM_ENABLED);
#endif
#if defined(MBEDTLS_SSL_CBC_RECORD_SPLITTING)
    mbedtls_ssl_conf_cbc_record_splitting( &conf, (options & 0x40) ? MBEDTLS_SSL_CBC_RECORD_SPLITTING_ENABLED : MBEDTLS_SSL_CBC_RECORD_SPLITTING_DISABLED );
#endif
#if defined(MBEDTLS_SSL_RENEGOTIATION)
    mbedtls_ssl_conf_renegotiation( &conf, (options & 0x80) ? MBEDTLS_SSL_RENEGOTIATION_ENABLED : MBEDTLS_SSL_RENEGOTIATION_DISABLED );
#endif
#if defined(MBEDTLS_SSL_SESSION_TICKETS)
    mbedtls_ssl_conf_session_tickets( &conf, (options & 0x100) ? MBEDTLS_SSL_SESSION_TICKETS_DISABLED : MBEDTLS_SSL_SESSION_TICKETS_ENABLED );
#endif
#if defined(MBEDTLS_SSL_ALPN)
    if (options & 0x200) {
        mbedtls_ssl_conf_alpn_protocols( &conf, alpn_list );
    }
#endif
    //There may be other options to add :
    // mbedtls_ssl_conf_cert_profile, mbedtls_ssl_conf_sig_hashes

    srand(1);
    mbedtls_ssl_conf_rng( &conf, dummy_random, &ctr_drbg );

    if( mbedtls_ssl_setup( &ssl, &conf ) != 0 )
        goto exit;

#if defined(MBEDTLS_X509_CRT_PARSE_C)
    if ((options & 1) == 0) {
        if( mbedtls_ssl_set_hostname( &ssl, "localhost" ) != 0 )
            goto exit;
    }
#endif

    biomemfuzz.Data = Data;
    biomemfuzz.Size = Size-2;
    biomemfuzz.Offset = 0;
    mbedtls_ssl_set_bio( &ssl, &biomemfuzz, dummy_send, fuzz_recv, NULL );

    ret = mbedtls_ssl_handshake( &ssl );
    if( ret == 0 )
    {
        //keep reading data from server until the end
        do
        {
            len = sizeof( buf ) - 1;
            ret = mbedtls_ssl_read( &ssl, buf, len );

            if( ret == MBEDTLS_ERR_SSL_WANT_READ )
                continue;
            else if( ret <= 0 )
                //EOF or error
                break;
        }
        while( 1 );
    }

exit:
    mbedtls_entropy_free( &entropy );
    mbedtls_ctr_drbg_free( &ctr_drbg );
    mbedtls_ssl_config_free( &conf );
    mbedtls_ssl_free( &ssl );

    return 0;
}
