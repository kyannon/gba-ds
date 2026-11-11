#define PROP_VALUE_MAX 92
#include <string.h>
static inline int __system_property_get(const char* k, char* v) { strcpy(v, strstr(k, "model") ? "RG DS Plus (stub)" : "x"); return 1; }
