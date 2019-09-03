#pragma once

#include "ble_instance.h"

#include "entt.h"
#include "sensor.h"


namespace opkey {


namespace blecfg {

inline ble::Array<64> midiPacket{};

using namespace ble;
namespace chr = ble::characteristic_options;
namespace svr = ble::server_options;

using MidiSvcUuid = Uuid128<0x03b80e5a, 0xede8, 0x4b33, 0xa751, 0x6ce34ec4c700>;
using MidiChrUuid = Uuid128<0x7772e5db, 0x3868, 0x4112, 0xa1a9, 0xf2669d106bf3>;

using MidiService = Service
	< MidiSvcUuid
	, Characteristic
		< MidiChrUuid
		, chr::BindArray<&midiPacket>
		, chr::Notify
		, chr::WriteNoResponse
		>
	>;

using Server = ble::Server
	< MidiService
	, svr::AdvertiseUuid<MidiService::Uuid>
	>;

} // namespace blecfg


class Application;
class SensorManager;

class BleInterface {
public:
	BleInterface(Application& application);

	BleInterface(const BleInterface&) = default;
	BleInterface(BleInterface&&) = default;
	BleInterface& operator=(const BleInterface&) = delete;
	BleInterface& operator=(BleInterface&&) = delete;

	void OnTick();
	void OnSensorStateChange(const SensorManager& sensorManager, Sensor sensor);

	//void OnKeyPressed(Key key, double velocity, ) {
	//}
	//void OnKeyReleased(Key key, ) {
	//}

private:
	entt::scoped_connection onTickConnection;
	entt::scoped_connection onSensorStateChangeConnection;

	ble::Instance<blecfg::Server> bleInstance{"OpKey"};
};


} // namespace opkey
