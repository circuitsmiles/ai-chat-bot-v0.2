#pragma once

// The extern "C" block ensures that both the type definition from nvs_flash.h 
// and the external variable declaration are treated using C linkage rules.
#ifdef __cplusplus
extern "C" {
#endif
    
#include <nvs_flash.h>
    
// Declaration: Using a unique name 'g_trinity_nvs_handle' to avoid symbol conflicts.
extern nvs_handle_t g_trinity_nvs_handle;

#ifdef __cplusplus
}
#endif
