#pragma once

#include "Controls.h"

#include <earshot/earshot.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace earshot::testing
{
class CaptureBackend;
}

/**
    Onsets from earshot (a snapshot in external/earshot; SNAPSHOT says which
    commit), for Trigger On = Kick (the lowest band's onsets).

    **CoreAudio is touched only when it has to be** (SPEC-snowglobe.md,
    Triggers). The constructor lists nothing and opens nothing: the menu
    starts as Resolume FFT, System default and spares. The plugin calls
    `EnsureListed()` and `Apply()` from the frame, and only while Trigger On is
    Kick, so a composition saved with Beat or Button never reaches CoreAudio,
    whatever its Audio Input says (the host restores parameters after the
    constructor and before the first frame). With SNOWGLOBE_NO_DEVICES=1 (set
    by tools/verify.sh and by the harness) nothing is ever listed, looked up
    or opened. sgtest --coreaudio proves both with an interposer.

    **Resolume's FFT** (element 0, the default): earshot's host fallback gives
    band onsets once a frame, frame-quantised and uncalibrated (earshot's
    INTEGRATING.md). **A device**: earshot's capture, its onsets timed on the
    audio clock inside the frame.

    The device menu follows rubens/spasis: element 0 = 0, element 1 =
    deviceKey( kDefaultDevice ), then each device with deviceKey( name ), then
    "(none)" spares (value 0), kInputSlots in all.
*/
namespace snowglobe
{

using BackendFactory = std::unique_ptr< earshot::testing::CaptureBackend > ( * )();

struct DeviceSlot
{
	std::string label;///< what the menu shows (30 characters at most)
	std::string name; ///< what selectDevice is given ("" for the host FFT)
	float value;      ///< the element's value: the device key
};

/// SNOWGLOBE_NO_DEVICES set (and not "0").
bool DevicesDisabled();

class AudioInput
{
public:
	/// For the harness: every AudioInput constructed afterwards uses this
	/// backend (a scripted device) and lists ITS devices. Never set by the plugin.
	static void SetBackendFactoryForTest( BackendFactory factory );

	AudioInput();
	~AudioInput();

	/// List the devices once, if allowed. True when the slots changed.
	bool EnsureListed();
	const std::vector< DeviceSlot >& Slots() const
	{
		return slots;
	}

	/// The Audio Input value the host stored. Stored only: nothing opens.
	void Want( float value )
	{
		wanted = value;
	}
	/// Open (or close) what is wanted. From the frame, only under Kick.
	void Apply();
	/// Under Beat or Button: close a device that is open (nothing if none is).
	void Release();

	/// Analyse this frame. `hostSeconds` is the plugin's clock.
	const earshot::Frame& Update( double hostSeconds, const float* bins, int binCount );
	const earshot::Frame& Frame() const;
	bool Open() const
	{
		return !applied.empty();
	}

	//-- Harness ------------------------------------------------------------------
	/// Audio as if from a device, without one: earshot's feed().
	void Feed( const float* interleaved, int frames, int rate, int channels );

private:
	std::unique_ptr< earshot::Engine > engine;
	std::vector< DeviceSlot > slots;
	std::map< float, std::string > seen;///< every device key met, and its name
	float wanted        = 0.0f;
	std::string applied;///< the name earshot was last given ("" none)
	bool listed         = false;
	bool feeding        = false;
};

} // namespace snowglobe
