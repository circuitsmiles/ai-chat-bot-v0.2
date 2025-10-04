#include "nvs_globals.h"

// This defines (allocates) the global NVS handle variable, 
// linking it to the declaration in the header file.
// Renamed from 'nvs_handle' to 'g_trinity_nvs_handle' to resolve linkage conflicts.
nvs_handle_t g_trinity_nvs_handle;
