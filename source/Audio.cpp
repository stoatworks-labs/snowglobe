#include "Audio.h"

#include "Diag.h"

#include <earshot/testing.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace snowglobe
{
namespace
{
BackendFactory g_factory = nullptr;

/// Onset bands, Hz: two octaves each from 40 Hz (rubens's edges), so a kick,
/// a snare's body, a snare's crack and a hat are each a band of their own.
constexpr float kBandEdges[ 5 ] = { 40.0f, 160.0f, 640.0f, 2560.0f, 8000.0f };
constexpr int kBands            = 4;

earshot::Config makeConfig()
{
	earshot::Config c;
	c.maxChannels = 2;
	c.bandCount   = kBands;
	for( int i = 0; i <= kBands; ++i )
		c.bandEdges[ i ] = kBandEdges[ i ];
	return c;
}
} // namespace

bool DevicesDisabled()
{
	const char* v = std::getenv( "SNOWGLOBE_NO_DEVICES" );
	return v != nullptr && *v != '\0' && std::strcmp( v, "0" ) != 0;
}

void AudioInput::SetBackendFactoryForTest( BackendFactory factory )
{
	g_factory = factory;
}

AudioInput::AudioInput()
{
	//The engine opens nothing until selectDevice: its miniaudio context is
	//made in open() and enumerate(), never here (earshot src/Miniaudio.cpp).
	if( g_factory != nullptr )
		engine = std::make_unique< earshot::Engine >( makeConfig(), g_factory() );
	else
		engine = std::make_unique< earshot::Engine >( makeConfig() );
	if( !engine->ok() )
		diag::error( "earshot could not allocate: no onsets" );
	slots.push_back( { kHostFftLabel, "", 0.0f } );
	slots.push_back( { kDefaultLabel, earshot::kDefaultDevice, earshot::deviceKey( earshot::kDefaultDevice ) } );
	while( static_cast< int >( slots.size() ) < kInputSlots )
		slots.push_back( { kEmptySlot, "", 0.0f } );
}

AudioInput::~AudioInput() = default;

bool AudioInput::EnsureListed()
{
	if( listed )
		return false;
	listed = true;
	std::vector< earshot::DeviceInfo > devices;
	if( g_factory != nullptr )
	{
		//A throwaway backend of the harness's kind: it lists, it opens nothing.
		if( auto lister = g_factory() )
			devices = lister->enumerate();
	}
	else if( !DevicesDisabled() )
		devices = earshot::listDevices();
	else
		return false;
	slots.resize( 2 );
	for( const earshot::DeviceInfo& d : devices )
	{
		if( static_cast< int >( slots.size() ) >= kInputSlots )
			break;
		const float key = earshot::deviceKey( d.name.c_str() );
		seen[ key ]     = d.name;
		slots.push_back( { d.name.substr( 0, 30 ), d.name, key } );
	}
	while( static_cast< int >( slots.size() ) < kInputSlots )
		slots.push_back( { kEmptySlot, "", 0.0f } );
	diag::info( "audio inputs: " + std::to_string( devices.size() ) + " device(s) listed" );
	return true;
}

void AudioInput::Apply()
{
	std::string name;
	if( wanted == 0.0f )
		name.clear();
	else if( wanted == earshot::deviceKey( earshot::kDefaultDevice ) )
		name = earshot::kDefaultDevice;
	else
	{
		const auto known = seen.find( wanted );
		//A key with no device met in this process: a name that cannot match,
		//so earshot says Missing (never the default).
		name = known != seen.end() ? known->second : "(device " + std::to_string( static_cast< long >( wanted ) ) + " not present)";
	}
	//SNOWGLOBE_NO_DEVICES=1: never opened, nor even looked up (earshot's lookup
	//itself starts CoreAudio). Resolume's FFT.
	if( !name.empty() && g_factory == nullptr && DevicesDisabled() )
		name.clear();
	if( name == applied )
		return;
	applied = name;
	feeding = false;//any select ends a feed
	engine->selectDevice( name.c_str() );
	diag::info( "audio input: " + ( name.empty() ? std::string( "Resolume FFT" ) : name ) );
}

void AudioInput::Release()
{
	if( applied.empty() )
		return;
	applied.clear();
	engine->selectDevice( "" );
	diag::info( "audio input closed: Trigger On is not Kick" );
}

void AudioInput::SetHostRate( unsigned int rate )
{
	if( rate < 8000 || rate > 768000 || rate == hostRate )
		return;
	hostRate         = rate;
	earshot::Config c = makeConfig();
	c.hostBins.sampleRate = static_cast< double >( rate );
	engine->configure( c );
	diag::info( "host audio at " + std::to_string( rate ) + " Hz: Resolume's FFT bins are " + std::to_string( rate / 128u ) + " Hz each" );
}

void AudioInput::SetHostCentreOnlyForTest( bool on )
{
	engine->mutations().hostCentreOnly = on;
}

double AudioInput::HostRateForTest() const
{
	return engine->config().hostBins.sampleRate;
}

const earshot::Frame& AudioInput::Update( double hostSeconds, const float* bins, int binCount )
{
	return engine->update( hostSeconds, bins, binCount );
}

const earshot::Frame& AudioInput::Frame() const
{
	return engine->frame();
}

void AudioInput::Feed( const float* interleaved, int frames, int rate, int channels )
{
	if( !feeding )
	{
		engine->feedBegin( rate, channels );
		feeding = true;
	}
	engine->feed( interleaved, frames );
}

} // namespace snowglobe
