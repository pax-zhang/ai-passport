#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "app_ota_logic.h"

int main(void)
{
    app_ota_ver_t a, b;
    app_ota_manifest_t m;
    uint8_t digest[32];

    assert(app_ota_parse_ver("0.1.0", &a));
    assert(a.major == 0 && a.minor == 1 && a.patch == 0);
    assert(app_ota_parse_ver("v1.2.3", &b));
    assert(b.major == 1 && b.minor == 2 && b.patch == 3);
    assert(app_ota_parse_ver("2.0.0-rc1", &a));
    assert(a.major == 2 && a.minor == 0 && a.patch == 0);
    assert(app_ota_parse_ver("3.4", &a));
    assert(a.major == 3 && a.minor == 4 && a.patch == 0);
    assert(!app_ota_parse_ver("", &a));
    assert(!app_ota_parse_ver("abc", &a));

    assert(app_ota_is_newer("0.1.0", "0.1.1"));
    assert(app_ota_is_newer("0.1.9", "0.2.0"));
    assert(app_ota_is_newer("0.9.9", "1.0.0"));
    assert(!app_ota_is_newer("0.2.0", "0.1.9"));
    assert(!app_ota_is_newer("0.1.0", "0.1.0"));
    assert(!app_ota_is_newer("bad", "0.1.0"));

    assert(strstr(APP_OTA_MANIFEST_URL, "/demo/iphone/ota/demo/iphone/latest.json"));
    assert(strstr(APP_OTA_MANIFEST_URL_ALT, "@demo/iphone/ota/demo/iphone/latest.json"));

    assert(app_ota_url_ok("https://example.com/a.bin"));
    assert(app_ota_url_ok(
        "https://github.com/pax-zhang/ai-passport/releases/download/demo-meow-v0.2.0/FoloToy-AI-Passport-demo-meow.bin"));
    assert(!app_ota_url_ok("http://example.com/a.bin"));
    assert(!app_ota_url_ok("https://"));
    assert(!app_ota_url_ok(
        "https://github.com/pax-zhang/ai-passport/releases/download/demo-meow-v0.2.0/FoloToy-AI-Passport-demo-meow-factory.bin"));

    memset(digest, 0, sizeof(digest));
    assert(app_ota_sha_match(
        "0000000000000000000000000000000000000000000000000000000000000000",
        digest));
    digest[0] = 0xab;
    assert(app_ota_sha_match(
        "ab00000000000000000000000000000000000000000000000000000000000000",
        digest));
    assert(app_ota_sha_match(
        "AB00000000000000000000000000000000000000000000000000000000000000",
        digest));
    assert(!app_ota_sha_match(
        "0000000000000000000000000000000000000000000000000000000000000000",
        digest));
    assert(!app_ota_sha_match("short", digest));

    assert(app_ota_parse_manifest(
        "{\n"
        "  \"channel\": \"demo/iphone\",\n"
        "  \"version\": \"0.2.0\",\n"
        "  \"url\": \"https://github.com/pax-zhang/ai-passport/releases/download/demo-meow-v0.2.0/a.bin\",\n"
        "  \"sha256\": \"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\",\n"
        "  \"size\": 1800000\n"
        "}", &m));
    assert(strcmp(m.channel, "demo/iphone") == 0);
    assert(strcmp(m.version, "0.2.0") == 0);
    assert(m.size == 1800000);
    assert(app_ota_is_newer("0.1.0", m.version));
    assert(app_ota_channel_ok(m.channel, APP_OTA_CHANNEL));
    assert(!app_ota_channel_ok(m.channel, "demo/tetris-game"));
    assert(!app_ota_channel_ok(m.channel, "main"));

    assert(!app_ota_parse_manifest("{\"version\":\"0.2.0\"}", &m));
    assert(!app_ota_parse_manifest(
        "{\n"
        "  \"channel\": \"demo/tetris-game\",\n"
        "  \"version\": \"0.2.0\",\n"
        "  \"url\": \"https://example.com/a.bin\",\n"
        "  \"sha256\": \"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\"\n"
        "}", &m) || !app_ota_channel_ok(m.channel, APP_OTA_CHANNEL));
    assert(!app_ota_parse_manifest(
        "{\"version\":\"0.2.0\",\"url\":\"http://x/a.bin\","
        "\"sha256\":\"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\"}",
        &m));
    assert(!app_ota_parse_manifest(
        "{\n"
        "  \"channel\": \"demo/iphone\",\n"
        "  \"version\": \"0.2.0\",\n"
        "  \"url\": \"https://example.com/FoloToy-AI-Passport-demo-meow-factory.bin\",\n"
        "  \"sha256\": \"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\",\n"
        "  \"size\": 1800000\n"
        "}", &m));
    assert(app_ota_parse_manifest(
        "{\n"
        "  \"channel\": \"demo/iphone\",\n"
        "  \"version\": \"0.2.0\",\n"
        "  \"url\": \"https://example.com/FoloToy-AI-Passport-demo-meow-factory.bin\",\n"
        "  \"ota_url\": \"https://example.com/FoloToy-AI-Passport-demo-meow.bin\",\n"
        "  \"sha256\": \"ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff\",\n"
        "  \"ota_sha256\": \"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\",\n"
        "  \"size\": 3000000,\n"
        "  \"ota_size\": 1800000,\n"
        "  \"factory_url\": \"https://example.com/FoloToy-AI-Passport-demo-meow-factory.bin\",\n"
        "  \"factory_sha256\": \"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\",\n"
        "  \"factory_size\": 2750000\n"
        "}", &m));
    assert(strcmp(m.url,
        "https://example.com/FoloToy-AI-Passport-demo-meow.bin") == 0);
    assert(strcmp(m.sha256,
        "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef") == 0);
    assert(m.size == 1800000);

    puts("ok");
    return 0;
}
