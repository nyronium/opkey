#include "ble.h"
#include "fmt.h"

#include <esp_nimble_hci.h>
#include <nimble/nimble_port.h>
#include <nimble/nimble_port_freertos.h>
#include <host/ble_hs.h>
#include <host/ble_uuid.h>
#include <host/util/util.h>
#include <services/gap/ble_svc_gap.h>
#include <services/gatt/ble_svc_gatt.h>


namespace OpKey::Ble {


template<auto MetaTypeEnum>
struct MetaType { };


template<uint32_t A, uint16_t B, uint16_t C, uint16_t D, uint64_t E>
struct Uuid128 : private MetaType<MetaTypes::Uuid> {
	static_assert(E <= 0xffffffffffff, "Invalid service uuid128: Template argument 'E' is out of range");

	inline constexpr static const std::array<uint8_t, 16> Bytes =
		{ static_cast<uint8_t>(E >>  0)
		, static_cast<uint8_t>(E >>  8)
		, static_cast<uint8_t>(E >> 16)
		, static_cast<uint8_t>(E >> 24)
		, static_cast<uint8_t>(E >> 32)
		, static_cast<uint8_t>(E >> 40)
		, static_cast<uint8_t>(D >>  0)
		, static_cast<uint8_t>(D >>  8)
		, static_cast<uint8_t>(C >>  0)
		, static_cast<uint8_t>(C >>  8)
		, static_cast<uint8_t>(B >>  0)
		, static_cast<uint8_t>(B >>  8)
		, static_cast<uint8_t>(A >>  0)
		, static_cast<uint8_t>(A >>  8)
		, static_cast<uint8_t>(A >> 16)
		, static_cast<uint8_t>(A >> 24)
		};

	constexpr static uint16_t As16Bit() noexcept {
		return static_cast<uint16_t>(A);
	};
};

template<uint16_t A>
struct Uuid16 {
	inline constexpr static const std::array<uint8_t, 2> Bytes =
		{ static_cast<uint8_t>(A >> 0)
		, static_cast<uint8_t>(A >> 8)
		};

	constexpr static uint16_t As16Bit() noexcept {
		return A;
	};
};

struct BluetoothBaseUuid
	: public Uuid128<0x00000000, 0x0000, 0x1000, 0x8000, 0x00805f9b34fb>
{
	template<uint16_t A>
	using From16Bit = Uuid128<A, 0x0000, 0x1000, 0x8000, 0x00805f9b34fb>;
};


struct UuidAuto { };


template<typename Uuid, typename... Options>
struct Characteristic {
};

template<typename Uuid, typename... Characteristics>
struct Service {
	std::tuple<Characteristics...> characteristics{};
};

template<typename... Services>
struct GattServer {
	std::tuple<Services...> services{};
};


using MidiService = Service
	< Uuid128<0x2134543B, 0x1234, 0x1234, 0x1234, 0x1234567890ab>
	, Characteristic<UuidAuto, >
	>;


static ... OnGattCharacteristicAccessDispatcher() {
}


class Characteristic {
	virtual void onAccess() {};

private:
	uuid;
	flags;
};

class Service {
	Service& AddCharacteristic() {
	}

private:
	type;
	uuid;
	std::vector<Characteristic> characteristics{};
};

class GattServer {
	Service& AddService() {
	}

private:
	std::vector<Service> services{};
};


static_assert(ESP_OK == 0, "This module relies on the value of ESP_OK to be 0");

static bool bleInitialized = false;
static bool bleReady = false;



static int BleGapEvent(ble_gap_event* event, void* arg);
static uint8_t own_addr_type;

extern "C" {
	void ble_store_config_init();
};



static std::string AddrToString(uint8_t val[6]) {
	return fmt::format("{:02x}:{:02x}:{:02x}:{:02x}:{:02x}:{:02x}", val[0], val[1], val[2], val[3], val[4], val[5]);
}

static ble_uuid16_t UUID16_INIT(uint16_t val) {
	ble_uuid16_t ret{};
	ret.u.type = BLE_UUID_TYPE_16;
	ret.value = val;
	return ret;
}

static ble_uuid128_t UUID128_INIT(std::array<uint8_t, 16> value) {
	ble_uuid128_t ret{};
	ret.u.type = BLE_UUID_TYPE_128;
	std::copy(ret.value, ret.value + 16, value.begin());
	return ret;
}



// The vendor specific security test service consists of two characteristics:
//     o random-number-generator: generates a random 32-bit number each time
//       it is read.  This characteristic can only be read over an encrypted
//       connection.
//     o static-value: a single-byte characteristic that can always be read,
//       but can only be written over an encrypted connection.

// 59462f12-9543-9999-12c8-58b459a2712d
static const ble_uuid128_t gatt_svr_svc_sec_test_uuid =
	UUID128_INIT({0x2d, 0x71, 0xa2, 0x59, 0xb4, 0x58, 0xc8, 0x12,
			0x99, 0x99, 0x43, 0x95, 0x12, 0x2f, 0x46, 0x59});

// 5c3a659e-897e-45e1-b016-007107c96df6
static const ble_uuid128_t gatt_svr_chr_sec_test_rand_uuid =
	UUID128_INIT({0xf6, 0x6d, 0xc9, 0x07, 0x71, 0x00, 0x16, 0xb0,
			0xe1, 0x45, 0x7e, 0x89, 0x9e, 0x65, 0x3a, 0x5c});

// 5c3a659e-897e-45e1-b016-007107c96df7
static const ble_uuid128_t gatt_svr_chr_sec_test_static_uuid =
	UUID128_INIT({0xf7, 0x6d, 0xc9, 0x07, 0x71, 0x00, 0x16, 0xb0,
			0xe1, 0x45, 0x7e, 0x89, 0x9e, 0x65, 0x3a, 0x5c});

static uint8_t gatt_svr_sec_test_static_val;

static int OnGattCharacteristicAccessSecTest(uint16_t conn_handle, uint16_t attr_handle, ble_gatt_access_ctxt* ctxt, void* arg);

static const ble_gatt_svc_def gatt_svr_svcs[] = {
	{
		// Service: Security test.
		.type = BLE_GATT_SVC_TYPE_PRIMARY,
		.uuid = &gatt_svr_svc_sec_test_uuid.u,
		.characteristics = (ble_gatt_chr_def[])
		{ {
			  // Characteristic: Random number generator.
			  .uuid = &gatt_svr_chr_sec_test_rand_uuid.u,
			  .access_cb = OnGattCharacteristicAccessSecTest,
			  .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC,
		  }, {
			  // Characteristic: Static value.
			  .uuid = &gatt_svr_chr_sec_test_static_uuid.u,
			  .access_cb = OnGattCharacteristicAccessSecTest,
			  .flags = BLE_GATT_CHR_F_READ |
				  BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_ENC,
		  }, {
			  0, // No more characteristics in this service.
		  }
		},
	},

	{
		0, // No more services.
	},
};

static int GattWriteCharacteristic(os_mbuf* om, uint16_t min_len, uint16_t max_len,
		void* dst, uint16_t* len)
{
	uint16_t om_len;
	int rc;

	om_len = OS_MBUF_PKTLEN(om);
	if (om_len < min_len || om_len > max_len) {
		return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
	}

	rc = ble_hs_mbuf_to_flat(om, dst, max_len, len);
	if (rc != 0) {
		return BLE_ATT_ERR_UNLIKELY;
	}

	return 0;
}

static int OnGattCharacteristicAccessSecTest(uint16_t conn_handle, uint16_t attr_handle,
	ble_gatt_access_ctxt* ctxt,
	void* arg)
{
	const ble_uuid_t* uuid;
	int rand_num;
	int rc;

	uuid = ctxt->chr->uuid;

	// Determine which characteristic is being accessed by examining its
	// 128-bit UUID.

	if (ble_uuid_cmp(uuid, &gatt_svr_chr_sec_test_rand_uuid.u) == 0) {
		assert(ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR);

		// Respond with a 32-bit random number.
		rand_num = rand();
		rc = os_mbuf_append(ctxt->om, &rand_num, sizeof rand_num);
		return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
	}

	if (ble_uuid_cmp(uuid, &gatt_svr_chr_sec_test_static_uuid.u) == 0) {
		switch (ctxt->op) {
			case BLE_GATT_ACCESS_OP_READ_CHR:
				rc = os_mbuf_append(ctxt->om, &gatt_svr_sec_test_static_val,
						sizeof gatt_svr_sec_test_static_val);
				return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;

			case BLE_GATT_ACCESS_OP_WRITE_CHR:
				rc = GattWriteCharacteristic(ctxt->om,
						sizeof gatt_svr_sec_test_static_val,
						sizeof gatt_svr_sec_test_static_val,
						&gatt_svr_sec_test_static_val, NULL);
				return rc;

			default:
				assert(0);
				return BLE_ATT_ERR_UNLIKELY;
		}
	}

	// Unknown characteristic; the nimble stack should not have called this
	// function.
	assert(0);
	return BLE_ATT_ERR_UNLIKELY;
}

static void BleGattRegister(ble_gatt_register_ctxt* context, void* arg) {
	switch (context->op) {
		case BLE_GATT_REGISTER_OP_SVC:
			esp::logi("Registered service {:s} with handle {:d}"
					, UuidToStr(context->svc.svc_def->uuid)
					, context->svc.handle
					);
			break;

		case BLE_GATT_REGISTER_OP_CHR:
			esp::logi("Registered characteristic {:s} with def_handle={:d} and val_handle={:d}"
					, UuidToStr(context->chr.chr_def->uuid)
					, context->chr.def_handle
					, context->chr.val_handle
					);
			break;

		case BLE_GATT_REGISTER_OP_DSC:
			esp::logi("Registered descriptor {:s} with handle={:d}"
					, UuidToStr(context->dsc.dsc_def->uuid)
					, context->dsc.handle
					);
			break;

		default:
			esp::loge("Invalid argument to BleGattRegister(): context->op={:d}\nAborting.", context->op);
			abort();
			break;
	}
}

static void BleGattInit() {
	ble_svc_gap_init();
	ble_svc_gatt_init();

	esp::check(ble_gatts_count_cfg(gatt_svr_svcs), "ble_gatts_count_cfg()");
	esp::check(ble_gatts_add_svcs(gatt_svr_svcs), "ble_gatts_add_svcs()");
}


static void BlePrintConnDesc(ble_gap_conn_desc& desc) {
	esp::logi("handle={:d} our_ota_addr_type={:d} our_ota_addr={}"
			, desc.conn_handle, desc.our_ota_addr.type
			, AddrToString(desc.our_ota_addr.val));
	esp::logi("our_id_addr_type={:d} our_id_addr={}"
			, desc.our_id_addr.type
			, AddrToString(desc.our_id_addr.val));
	esp::logi("peer_ota_addr_type={:d} peer_ota_addr={}"
			, desc.peer_ota_addr.type
			, AddrToString(desc.peer_ota_addr.val));
	esp::logi("peer_id_addr_type={:d} peer_id_addr={}"
			, desc.peer_id_addr.type
			, AddrToString(desc.peer_id_addr.val));
	esp::logi("conn_itvl={:d} conn_latency={:d} supervision_timeout={:d} encrypted={} authenticated={} bonded={}\n"
			, desc.conn_itvl
			, desc.conn_latency
			, desc.supervision_timeout
			, static_cast<bool>(desc.sec_state.encrypted)
			, static_cast<bool>(desc.sec_state.authenticated)
			, static_cast<bool>(desc.sec_state.bonded));
}

static void BleEnableAdvertise() {
	ble_gap_adv_params adv_params{};
	ble_hs_adv_fields fields{};
	// Set the advertisement data included in our advertisements:
	//    o Flags (indicates advertisement type and other general info).
	//    o Advertising tx power.
	//    o Device name.
	//    o 16-bit service UUIDs (alert notifications).

	// Advertise two flags:
	//    o Discoverability in forthcoming advertisement (general)
	//    o BLE-only (BR/EDR unsupported).
	fields.flags = BLE_HS_ADV_F_DISC_GEN |
		BLE_HS_ADV_F_BREDR_UNSUP;

	// Indicate that the TX power level field should be included; have the
	// stack fill this value automatically.  This is done by assigning the
	// special value BLE_HS_ADV_TX_PWR_LVL_AUTO.
	fields.tx_pwr_lvl_is_present = 1;
	fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;

	const char* name = ble_svc_gap_device_name();
	// TODO const cast?!
	fields.name = const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(name));
	fields.name_len = strlen(name);
	fields.name_is_complete = 1;

	static ble_uuid16_t id = UUID16_INIT(0x1811);
	fields.uuids16 = &id;
	fields.num_uuids16 = 1;
	fields.uuids16_is_complete = 1;

	if (auto rc = ble_gap_adv_set_fields(&fields); rc != 0) {
		esp::loge("error setting advertisement data; rc={:d}\n", rc);
		return;
	}

	// Begin advertising.
	memset(&adv_params, 0, sizeof adv_params);
	adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
	adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
	auto rc = ble_gap_adv_start(own_addr_type, nullptr, BLE_HS_FOREVER,
			&adv_params, BleGapEvent, nullptr);
	if (rc != 0) {
		esp::loge("error enabling advertisement; rc={:d}\n", rc);
		return;
	}
}

/**
 * The nimble host executes this callback when a GAP event occurs.  The
 * application associates a GAP event callback with each connection that forms.
 * bleprph uses the same callback for all connections.
 *
 * @param event   The type of event being signalled.
 * @param ctxt    Various information pertaining to the event.
 * @param arg     Application-specified argument; unused by
 *                  bleprph.
 *
 * @return        0 if the application successfully handled the
 *                  event; nonzero on failure.  The semantics
 *                  of the return code is specific to the
 *                  particular GAP event being signalled.
 */
static int BleGapEvent(ble_gap_event* event, void* arg) {
	ble_gap_conn_desc desc;
	int rc;

	switch (event->type) {
		case BLE_GAP_EVENT_CONNECT:
			// A new connection was established or a connection attempt failed.
			esp::logi("connection {:s}; status={:d} ",
					event->connect.status == 0 ? "established" : "failed",
					event->connect.status);
			if (event->connect.status == 0) {
				rc = ble_gap_conn_find(event->connect.conn_handle, &desc);
				assert(rc == 0);
				BlePrintConnDesc(desc);
			}

			if (event->connect.status != 0) {
				// Connection failed; resume advertising.
				BleEnableAdvertise();
			}
			return 0;

		case BLE_GAP_EVENT_DISCONNECT:
			esp::logi("disconnect; reason={:d} ", event->disconnect.reason);
			BlePrintConnDesc(event->disconnect.conn);

			// Connection terminated; resume advertising.
			BleEnableAdvertise();
			return 0;

		case BLE_GAP_EVENT_CONN_UPDATE:
			// The central has updated the connection parameters.
			esp::logi("connection updated; status={:d} ",
					event->conn_update.status);
			rc = ble_gap_conn_find(event->connect.conn_handle, &desc);
			assert(rc == 0);
			BlePrintConnDesc(desc);
			return 0;

		case BLE_GAP_EVENT_ADV_COMPLETE:
			esp::logi("advertise complete; reason={:d}",
					event->adv_complete.reason);
			BleEnableAdvertise();
			return 0;

		case BLE_GAP_EVENT_ENC_CHANGE:
			// Encryption has been enabled or disabled for this connection.
			esp::logi("encryption change event; status={:d} ",
					event->enc_change.status);
			rc = ble_gap_conn_find(event->connect.conn_handle, &desc);
			assert(rc == 0);
			BlePrintConnDesc(desc);
			return 0;

		case BLE_GAP_EVENT_SUBSCRIBE:
			esp::logi("subscribe event; conn_handle={:d} attr_handle={:d} "
					"reason={:d} prevn={} curn={} previ={} curi={}\n",
					event->subscribe.conn_handle,
					event->subscribe.attr_handle,
					event->subscribe.reason,
					static_cast<bool>(event->subscribe.prev_notify),
					static_cast<bool>(event->subscribe.cur_notify),
					static_cast<bool>(event->subscribe.prev_indicate),
					static_cast<bool>(event->subscribe.cur_indicate));
			return 0;

		case BLE_GAP_EVENT_MTU:
			esp::logi("mtu update event; conn_handle={:d} cid={:d} mtu={:d}\n",
					event->mtu.conn_handle,
					event->mtu.channel_id,
					event->mtu.value);
			return 0;

		case BLE_GAP_EVENT_REPEAT_PAIRING:
			// We already have a bond with the peer, but it is attempting to
			// establish a new secure link.  This app sacrifices security for
			// convenience: just throw away the old bond and accept the new link.

			// Delete the old bond. */
			rc = ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc);
			assert(rc == 0);
			ble_store_util_delete_peer(&desc.peer_id_addr);

			// Return BLE_GAP_REPEAT_PAIRING_RETRY to indicate that the host should
			// continue with the pairing operation.
			return BLE_GAP_REPEAT_PAIRING_RETRY;

		case BLE_GAP_EVENT_PASSKEY_ACTION:
			esp::logi("PASSKEY_ACTION_EVENT started");
			ble_sm_io pkey = {0};
			int key = 0;

			if (event->passkey.params.action == BLE_SM_IOACT_DISP) {
				pkey.action = event->passkey.params.action;
				pkey.passkey = 123456; // This is the passkey to be entered on peer
				esp::logi("Enter passkey {:d} on the peer side", pkey.passkey);
				rc = ble_sm_inject_io(event->passkey.conn_handle, &pkey);
				esp::logi("ble_sm_inject_io result: {:d}\n", rc);
			} else if (event->passkey.params.action == BLE_SM_IOACT_NUMCMP) {
				esp::logi("Passkey on device's display: {:d}", event->passkey.params.numcmp);
				esp::logi("Accept or reject the passkey through console in this format -> key Y or key N");
				pkey.action = event->passkey.params.action;
				//if (scli_receive_key(&key)) {
				//	pkey.numcmp_accept = key;
				//} else {
				//	pkey.numcmp_accept = 0;
				//	esp::loge("Timeout! Rejecting the key");
				//}
				rc = ble_sm_inject_io(event->passkey.conn_handle, &pkey);
				esp::logi("ble_sm_inject_io result: {:d}\n", rc);
			} else if (event->passkey.params.action == BLE_SM_IOACT_OOB) {
				static uint8_t tem_oob[16] = {0};
				pkey.action = event->passkey.params.action;
				for (int i = 0; i < 16; i++) {
					pkey.oob[i] = tem_oob[i];
				}
				rc = ble_sm_inject_io(event->passkey.conn_handle, &pkey);
				esp::logi("ble_sm_inject_io result: {:d}\n", rc);
			} else if (event->passkey.params.action == BLE_SM_IOACT_INPUT) {
				esp::logi("Enter the passkey through console in this format-> key 123456");
				pkey.action = event->passkey.params.action;
				//if (scli_receive_key(&key)) {
				//	pkey.passkey = key;
				//} else {
				//	pkey.passkey = 0;
				//	esp::loge("Timeout! Passing 0 as the key");
				//}
				rc = ble_sm_inject_io(event->passkey.conn_handle, &pkey);
				esp::logi("ble_sm_inject_io result: {:d}\n", rc);
			}
			return 0;
	}

	return 0;
}

static void BleOnReset(int reason) {
	esp::loge("Resetting state; reason={:d}\n", reason);
	bleReady = false;
}

static void BleOnSync() {
	assert(ble_hs_util_ensure_addr(0) == 0);

	// Figure out address to use while advertising (no privacy for now)
	if (auto rc = ble_hs_id_infer_auto(0, &own_addr_type); rc != 0) {
		esp::loge("error determining address type; rc={:d}\n", rc);
		return;
	}

	bleReady = true;

	// Printing ADDR
	uint8_t addr_val[6] = {0};
	int rc = ble_hs_id_copy_addr(own_addr_type, addr_val, nullptr);

	esp::logi("Device Address: {}\n", AddrToString(addr_val));

	// Begin advertising.
	BleEnableAdvertise();
}


static void NimbleHostTask(void* param) {
	esp::logi("BLE Host Task Started");

	// This function will return only when nimble_port_stop() is executed
	nimble_port_run();
	nimble_port_freertos_deinit();
}


static void Init(const char* name) {
	if (bleInitialized) {
		throw std::runtime_error("BLE already initialized");
	}
	bleInitialized = true;

	esp::check(esp_nimble_hci_and_controller_init(), "esp_nimble_hci_and_controller_init()");
	nimble_port_init();

	// Initialize the NimBLE host configuration.
	ble_hs_cfg.reset_cb = BleOnReset;
	ble_hs_cfg.sync_cb = BleOnSync;
	ble_hs_cfg.gatts_register_cb = BleGattRegister;
	ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

	ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_NO_IO;
	ble_hs_cfg.sm_bonding = true;
	ble_hs_cfg.sm_mitm = true;
	ble_hs_cfg.sm_sc = true;
	ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC;
	ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC;

	BleGattInit();
	esp::check(ble_svc_gap_device_name_set(name), "ble_svc_gap_device_name_set()");

	ble_store_config_init();
	nimble_port_freertos_init(NimbleHostTask);
}


} // namespace OpKey::Ble
