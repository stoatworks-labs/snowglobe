/*
    audiospy -- what sgtest --coreaudio loads into a child sgtest with
    DYLD_INSERT_LIBRARIES, so "nothing touched CoreAudio" is counted, not
    assumed.

    It interposes dlopen (every load of CoreAudio, AudioUnit or AudioToolbox
    by path: miniaudio links CoreAudio at run time this way, earshot's
    miniaudio.h line 36584) and dlsym (every symbol whose name starts "Audio",
    through any handle), and the CoreAudio and AudioToolbox calls a capture
    has to make (the HAL's property calls, the component calls). Each is
    counted before anything else happens.

    AUDIOSPY_BLOCK=1 (sgtest always sets it) then refuses them: the dlopen
    returns NULL, the calls fail. So even a child whose gate has failed
    cannot open the microphone; the count is what reports it.

    The plugin's own CoreAudio and AudioToolbox are linked at load time
    (earshot's CMakeLists.txt), so the frameworks are mapped into every
    process that loads the plugin; what is measured is whether anything
    CALLS into them or loads them again by name.
*/
#include <AudioToolbox/AudioToolbox.h>
#include <CoreAudio/CoreAudio.h>
#include <dlfcn.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#define INTERPOSE( replacement, replacee )                                                                                                   \
	__attribute__( ( used ) ) static const struct                                                                                            \
	{                                                                                                                                         \
		const void* r;                                                                                                                        \
		const void* e;                                                                                                                        \
	} interpose_##replacee __attribute__( ( section( "__DATA,__interpose" ) ) ) = { ( const void* )&replacement, ( const void* )&replacee }

static atomic_int g_dlopens, g_dlsyms, g_calls;

static int blocking( void )
{
	const char* v = getenv( "AUDIOSPY_BLOCK" );
	return v != NULL && v[ 0 ] == '1';
}

static int audioFramework( const char* path )
{
	return path != NULL && ( strstr( path, "CoreAudio.framework" ) != NULL || strstr( path, "AudioUnit.framework" ) != NULL
	                         || strstr( path, "AudioToolbox.framework" ) != NULL );
}

/// The counts, for the child to print: dlsym( RTLD_DEFAULT, "audiospy_counts" ).
__attribute__( ( visibility( "default" ) ) ) void audiospy_counts( int* dlopens, int* dlsyms, int* calls )
{
	*dlopens = atomic_load( &g_dlopens );
	*dlsyms  = atomic_load( &g_dlsyms );
	*calls   = atomic_load( &g_calls );
}

static void* spy_dlopen( const char* path, int mode )
{
	if( audioFramework( path ) )
	{
		atomic_fetch_add( &g_dlopens, 1 );
		if( blocking() )
			return NULL;
	}
	return dlopen( path, mode );
}
INTERPOSE( spy_dlopen, dlopen );

static void* spy_dlsym( void* handle, const char* name )
{
	//The child's own lookup of the counts is not a CoreAudio symbol.
	if( name != NULL && strncmp( name, "Audio", 5 ) == 0 )
	{
		atomic_fetch_add( &g_dlsyms, 1 );
		if( blocking() )
			return NULL;
	}
	return dlsym( handle, name );
}
INTERPOSE( spy_dlsym, dlsym );

#define COUNT_OR_REFUSE( refused )                                                                                                           \
	do                                                                                                                                        \
	{                                                                                                                                         \
		atomic_fetch_add( &g_calls, 1 );                                                                                                      \
		if( blocking() )                                                                                                                      \
			return refused;                                                                                                                   \
	} while( 0 )

static OSStatus spy_GetPropertyData( AudioObjectID o, const AudioObjectPropertyAddress* a, UInt32 qs, const void* q, UInt32* size, void* data )
{
	COUNT_OR_REFUSE( kAudioHardwareUnspecifiedError );
	return AudioObjectGetPropertyData( o, a, qs, q, size, data );
}
INTERPOSE( spy_GetPropertyData, AudioObjectGetPropertyData );

static OSStatus spy_GetPropertyDataSize( AudioObjectID o, const AudioObjectPropertyAddress* a, UInt32 qs, const void* q, UInt32* size )
{
	COUNT_OR_REFUSE( kAudioHardwareUnspecifiedError );
	return AudioObjectGetPropertyDataSize( o, a, qs, q, size );
}
INTERPOSE( spy_GetPropertyDataSize, AudioObjectGetPropertyDataSize );

static Boolean spy_HasProperty( AudioObjectID o, const AudioObjectPropertyAddress* a )
{
	COUNT_OR_REFUSE( false );
	return AudioObjectHasProperty( o, a );
}
INTERPOSE( spy_HasProperty, AudioObjectHasProperty );

static OSStatus spy_SetPropertyData( AudioObjectID o, const AudioObjectPropertyAddress* a, UInt32 qs, const void* q, UInt32 size, const void* data )
{
	COUNT_OR_REFUSE( kAudioHardwareUnspecifiedError );
	return AudioObjectSetPropertyData( o, a, qs, q, size, data );
}
INTERPOSE( spy_SetPropertyData, AudioObjectSetPropertyData );

static OSStatus spy_AddListener( AudioObjectID o, const AudioObjectPropertyAddress* a, AudioObjectPropertyListenerProc p, void* c )
{
	COUNT_OR_REFUSE( kAudioHardwareUnspecifiedError );
	return AudioObjectAddPropertyListener( o, a, p, c );
}
INTERPOSE( spy_AddListener, AudioObjectAddPropertyListener );

static AudioComponent spy_FindNext( AudioComponent c, const AudioComponentDescription* d )
{
	COUNT_OR_REFUSE( NULL );
	return AudioComponentFindNext( c, d );
}
INTERPOSE( spy_FindNext, AudioComponentFindNext );

static OSStatus spy_InstanceNew( AudioComponent c, AudioComponentInstance* i )
{
	COUNT_OR_REFUSE( kAudioHardwareUnspecifiedError );
	return AudioComponentInstanceNew( c, i );
}
INTERPOSE( spy_InstanceNew, AudioComponentInstanceNew );
