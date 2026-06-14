#include "credentials.h"

#include <stdio.h>

static const char *const PASSWORD_WORDS[] = {
    "amber", "anchor", "apple", "artist", "atlas", "autumn", "badge", "baker",
    "bamboo", "beacon", "beaver", "berry", "bison", "blossom", "border", "bottle",
    "bridge", "bronze", "bucket", "butter", "cactus", "camera", "candle", "canyon",
    "castle", "cedar", "celery", "cement", "cherry", "circle", "clover", "cobalt",
    "coffee", "comet", "copper", "cotton", "cradle", "cricket", "crystal", "daisy",
    "dakota", "delta", "desert", "diamond", "dolphin", "dragon", "drift", "eagle",
    "earth", "echo", "ember", "engine", "falcon", "farm", "feather", "ferry",
    "field", "forest", "fossil", "garden", "ginger", "glacier", "golden", "grape",
    "harbor", "hazel", "helmet", "hollow", "honest", "horizon", "island", "ivory",
    "jacket", "jaguar", "jasmine", "jigsaw", "juniper", "kernel", "kettle", "kiwi",
    "ladder", "lagoon", "lantern", "laser", "lemon", "linen", "lizard", "magnet",
    "maple", "marble", "market", "meadow", "melon", "meteor", "mirror", "misty",
    "monkey", "museum", "nectar", "needle", "nickel", "oasis", "olive", "orange",
    "orbit", "orchid", "otter", "oxygen", "paddle", "paper", "parrot", "pebble",
    "pepper", "pickle", "planet", "pocket", "prairie", "quartz", "rabbit", "radar",
    "raven", "record", "river", "rocket", "saddle", "saffron", "sailor", "salmon",
    "saturn", "shadow", "silver", "signal", "sketch", "socket", "sparrow", "spider",
    "spring", "square", "stable", "station", "stone", "summer", "sunset", "tackle",
    "tango", "temple", "ticket", "timber", "tomato", "tunnel", "turkey", "turtle",
    "velvet", "violet", "voyage", "walnut", "wander", "window", "winter", "wizard",
    "yellow", "yonder", "zephyr", "zigzag", "acorn", "banana", "basket", "button",
    "carbon", "carrot", "coral", "denim", "donkey", "fabric", "finger", "flower",
    "galaxy", "garage", "garlic", "goblin", "granite", "hammer", "hazard", "icicle",
    "insect", "jungle", "kitten", "koala", "legend", "little", "lobster", "lunar",
    "memory", "mineral", "muffin", "napkin", "native", "noodle", "number", "onion",
    "opal", "palace", "pencil", "person", "pigeon", "pirate", "plasma", "potato",
    "puzzle", "quiver", "ribbon", "robot", "salsa", "school", "season", "shrimp",
    "simple", "singer", "smoke", "snow", "spirit", "sponge", "staple", "studio",
    "sugar", "switch", "tablet", "thunder", "tiger", "toast", "topaz", "travel",
    "triple", "trumpet", "velcro", "vendor", "vessel", "walrus", "water", "whisper",
    "willow", "winner", "yogurt", "zebra", "zenith", "zipper", "almond", "arcade",
    "arctic", "balance", "beetle", "breeze", "broker", "canvas", "casino", "citron",
    "cookie", "cosmic", "damage", "dinner", "doodle", "effect", "famous", "folder",
    "gentle", "glider", "guitar", "honor", "ignite", "jumper", "kingdom", "letter",
    "marine", "motion", "public", "random", "remote", "rescue", "secure", "server",
    "system", "tandem", "urgent", "vector", "vision", "volume", "writer", "zodiac",
};

enum {
    PASSWORD_WORD_COUNT = sizeof(PASSWORD_WORDS) / sizeof(PASSWORD_WORDS[0]),
};

credentials_result_t credentials_generate_human_password(
    char *out,
    size_t out_size,
    credentials_random_fn_t random_fn,
    void *random_ctx)
{
    if (out == NULL || out_size == 0 || random_fn == NULL) {
        return CREDENTIALS_ERR_INVALID_ARG;
    }

    uint32_t word_indexes[CREDENTIALS_PASSWORD_WORD_COUNT] = {0};
    if (!random_fn((uint8_t *)word_indexes, sizeof(word_indexes), random_ctx)) {
        return CREDENTIALS_ERR_RANDOM_FAILED;
    }

    const int password_len = snprintf(
        out,
        out_size,
        "%s-%s-%s-%s-%s-%s-%s-%s",
        PASSWORD_WORDS[word_indexes[0] % PASSWORD_WORD_COUNT],
        PASSWORD_WORDS[word_indexes[1] % PASSWORD_WORD_COUNT],
        PASSWORD_WORDS[word_indexes[2] % PASSWORD_WORD_COUNT],
        PASSWORD_WORDS[word_indexes[3] % PASSWORD_WORD_COUNT],
        PASSWORD_WORDS[word_indexes[4] % PASSWORD_WORD_COUNT],
        PASSWORD_WORDS[word_indexes[5] % PASSWORD_WORD_COUNT],
        PASSWORD_WORDS[word_indexes[6] % PASSWORD_WORD_COUNT],
        PASSWORD_WORDS[word_indexes[7] % PASSWORD_WORD_COUNT]);
    if (password_len < 0 || password_len >= (int)out_size) {
        return CREDENTIALS_ERR_OUTPUT_TOO_SMALL;
    }
    return CREDENTIALS_OK;
}

credential_rotation_policy_result_t credential_rotation_policy_evaluate(
    bool local_display_ready,
    bool persistence_allowed)
{
    if (!local_display_ready) {
        return CREDENTIAL_ROTATION_REJECT_NO_LOCAL_DISPLAY;
    }
    if (!persistence_allowed) {
        return CREDENTIAL_ROTATION_REJECT_PERSISTENCE_UNSAFE;
    }
    return CREDENTIAL_ROTATION_ACCEPT;
}

const char *credential_rotation_policy_result_name(credential_rotation_policy_result_t result)
{
    switch (result) {
        case CREDENTIAL_ROTATION_ACCEPT:
            return "accept";
        case CREDENTIAL_ROTATION_REJECT_NO_LOCAL_DISPLAY:
            return "no_local_display";
        case CREDENTIAL_ROTATION_REJECT_PERSISTENCE_UNSAFE:
            return "persistence_unsafe";
        default:
            return "unknown";
    }
}
