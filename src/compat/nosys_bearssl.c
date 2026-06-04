/* BearSSL link stub for targets that exclude sysrng.c from the archive
 * (xbox/nxdk, wii/devkitPPC). ssl_engine.c pulls br_prng_seeder_system via
 * its auto-seed path; tls_client.c always injects entropy manually, so this
 * is link-only. */
#include "bearssl.h"

br_prng_seeder br_prng_seeder_system(const char **name)
{
    if (name) *name = "none";
    return 0;
}
