#include "nvs.h"
#include "fmt.h"

#include <nvs_flash.h>


namespace OpKey::Nvs {


static void Init() {
	/* Initialize NVS (used to store opkey and bluetooth PHY calibration data) */
	if (nvs_flash_init() != ESP_OK) {
		esp::check(nvs_flash_erase(), "nvs_flash_erase()");
		esp::check(nvs_flash_init(), "nvs_flash_init()");
	}
}


} // namespace OpKey::Nvs
