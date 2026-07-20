/*=============================================================================
	XeAudioDevice.cpp: Unreal XAudio2 Audio interface object.
	Copyright 1998-2013 Epic Games, Inc. All Rights Reserved.

	Unreal is RHS with Y and Z swapped (or technically LHS with flipped axis)

=============================================================================*/

/*------------------------------------------------------------------------------------
	Audio includes.
------------------------------------------------------------------------------------*/

#include "Engine.h"

#include <xapofx.h>
#include <xaudio2fx.h>

#include "EngineSoundClasses.h"
#include "EngineAudioDeviceClasses.h"
#include "UnAudioEffect.h"
#include "XAudio2Device.h"
#include "XAudio2Effects.h"

/*------------------------------------------------------------------------------------
	FXeAudioEffectsManager.
------------------------------------------------------------------------------------*/

/** 
 * Create voices that pipe the dry or EQ'd sound to the master output
 */
UBOOL FXAudio2EffectsManager::CreateEQPremasterVoices( UXAudio2Device* XAudio2Device )
{
	DWORD SampleRate = XAudio2Device->DeviceDetails.OutputFormat.Format.nSamplesPerSec;

	// Create the EQ effect
	if( !AudioDevice->ValidateAPICall( TEXT( "CreateFX (EQ)" ), 
		CreateFX( __uuidof( FXEQ ), &EQEffect ) ) )
	{
		return( FALSE );
	}

	XAUDIO2_EFFECT_DESCRIPTOR EQEffects[] =
	{
		{ EQEffect, TRUE, SPEAKER_COUNT }
	};

	XAUDIO2_EFFECT_CHAIN EQEffectChain =
	{
		1, EQEffects
	};

	if( !AudioDevice->ValidateAPICall( TEXT( "CreateSubmixVoice (EQPremaster)" ), 
		XAudio2Device->XAudio2->CreateSubmixVoice( &EQPremasterVoice, SPEAKER_COUNT, SampleRate, 0, STAGE_EQPREMASTER, NULL, &EQEffectChain ) ) )
	{
		return( FALSE );
	}

	if( !AudioDevice->ValidateAPICall( TEXT( "CreateSubmixVoice (DryPremaster)" ), 
		XAudio2Device->XAudio2->CreateSubmixVoice( &DryPremasterVoice, SPEAKER_COUNT, SampleRate, 0, STAGE_EQPREMASTER, NULL, NULL ) ) )
	{
		return( FALSE );
	}

	// Set the output matrix catering for a potential downmix
	const DWORD NumChannels = XAudio2Device->DeviceDetails.OutputFormat.Format.nChannels;
	UXAudio2Device::GetOutputMatrix( XAudio2Device->DeviceDetails.OutputFormat.dwChannelMask, NumChannels );

	if( !AudioDevice->ValidateAPICall( TEXT( "SetOutputMatrix (EQPremaster)" ), 
		EQPremasterVoice->SetOutputMatrix( NULL, SPEAKER_COUNT, NumChannels, UXAudio2Device::OutputMixMatrix ) ) )
	{
		return( FALSE );
	}

	if( !AudioDevice->ValidateAPICall( TEXT( "SetOutputMatrix (DryPremaster)" ), 
		DryPremasterVoice->SetOutputMatrix( NULL, SPEAKER_COUNT, NumChannels, UXAudio2Device::OutputMixMatrix ) ) )
	{
		return( FALSE );
	}

	return( TRUE );
}

/** 
 * Create a voice that pipes the reverb sounds to the premastering voices
 */
UBOOL FXAudio2EffectsManager::CreateReverbVoice( UXAudio2Device* XAudio2Device )
{
	UINT32 Flags;

	DWORD SampleRate = XAudio2Device->DeviceDetails.OutputFormat.Format.nSamplesPerSec;
	Flags = 0;		// XAUDIO2FX_DEBUG

	// Create the reverb effect
	if( !AudioDevice->ValidateAPICall( TEXT( "CreateReverbEffect" ), 
		XAudio2CreateReverb( &ReverbEffect, Flags ) ) )
	{
		return( FALSE );
	}

	XAUDIO2_EFFECT_DESCRIPTOR ReverbEffects[] =
	{
		{ ReverbEffect, TRUE, 2 }
	};

	XAUDIO2_EFFECT_CHAIN ReverbEffectChain =
	{
		1, ReverbEffects
	};

	XAUDIO2_SEND_DESCRIPTOR SendList[] = 
	{
		{ 0, DryPremasterVoice }
	};

	const XAUDIO2_VOICE_SENDS ReverbSends = 
	{
		1, SendList
	};

	if( !AudioDevice->ValidateAPICall( TEXT( "CreateSubmixVoice (Reverb)" ), 
		XAudio2Device->XAudio2->CreateSubmixVoice( &ReverbEffectVoice, 2, SampleRate, 0, STAGE_REVERB, &ReverbSends, &ReverbEffectChain ) ) )
	{
		return( FALSE );
	}

	const FLOAT OutputMatrix[SPEAKER_COUNT * 2] = 
	{ 
		1.0f, 0.0f,
		0.0f, 1.0f, 
		0.7f, 0.7f, 
		0.0f, 0.0f, 
		1.0f, 0.0f, 
		0.0f, 1.0f 
	};

	if( !AudioDevice->ValidateAPICall( TEXT( "SetOutputMatrix (Reverb)" ), 
		ReverbEffectVoice->SetOutputMatrix( DryPremasterVoice, 2, SPEAKER_COUNT, OutputMatrix ) ) )
	{
		return( FALSE );
	}

	return( TRUE );
}

/**
 * Init all sound effect related code
 */
FXAudio2EffectsManager::FXAudio2EffectsManager( UAudioDevice* InDevice )
	: FAudioEffectsManager( InDevice )
{
	UXAudio2Device* XAudio2Device = ( UXAudio2Device* )AudioDevice;
	
	check( MIN_FILTER_GAIN >= FXEQ_MIN_GAIN );
	check( MAX_FILTER_GAIN <= FXEQ_MAX_GAIN );
	check( MIN_FILTER_FREQUENCY >= FXEQ_MIN_FREQUENCY_CENTER );
	check( MAX_FILTER_FREQUENCY <= FXEQ_MAX_FREQUENCY_CENTER );

	ReverbEffect = NULL;
	EQEffect = NULL;

	DryPremasterVoice = NULL;
	EQPremasterVoice = NULL;
	ReverbEffectVoice = NULL;

	// Create premaster voices for EQ and dry passes
	CreateEQPremasterVoices( XAudio2Device );

	// Create reverb voice 
	CreateReverbVoice( XAudio2Device );
}

/**
 * Clean up
 */
FXAudio2EffectsManager::~FXAudio2EffectsManager( void )
{
	if( ReverbEffectVoice )
	{
		ReverbEffectVoice->DestroyVoice();
	}

	if( DryPremasterVoice )
	{
		DryPremasterVoice->DestroyVoice();
	}

	if( EQPremasterVoice )
	{
		EQPremasterVoice->DestroyVoice();
	}

	if( ReverbEffect )
	{
		ReverbEffect->Release();
	}

	if( EQEffect )
	{
		EQEffect->Release();
	}
}

/**
 * Applies the generic reverb parameters to the XAudio2 hardware
 */
void FXAudio2EffectsManager::SetReverbEffectParameters( const FAudioReverbEffect& ReverbEffectParameters )
{
	if( ReverbEffectVoice != NULL )
	{
		XAUDIO2FX_REVERB_I3DL2_PARAMETERS ReverbParameters;
		XAUDIO2FX_REVERB_PARAMETERS NativeParameters;

		ReverbParameters.WetDryMix = 100.0f;
		ReverbParameters.Room = VolumeToMilliBels( ReverbEffectParameters.Volume * ReverbEffectParameters.Gain, 0 );
		ReverbParameters.RoomHF = VolumeToMilliBels( ReverbEffectParameters.GainHF, -45 );
		ReverbParameters.RoomRolloffFactor = ReverbEffectParameters.RoomRolloffFactor;
		ReverbParameters.DecayTime = ReverbEffectParameters.DecayTime;
		ReverbParameters.DecayHFRatio = ReverbEffectParameters.DecayHFRatio;
		ReverbParameters.Reflections = VolumeToMilliBels( ReverbEffectParameters.ReflectionsGain, 1000 );
		ReverbParameters.ReflectionsDelay = ReverbEffectParameters.ReflectionsDelay;
		ReverbParameters.Reverb = VolumeToMilliBels( ReverbEffectParameters.LateGain, 2000 );
		ReverbParameters.ReverbDelay = ReverbEffectParameters.LateDelay;
		ReverbParameters.Diffusion = ReverbEffectParameters.Diffusion * 100.0f;
		ReverbParameters.Density = ReverbEffectParameters.Density * 100.0f;
		ReverbParameters.HFReference = DEFAULT_HIGH_FREQUENCY;

		ReverbConvertI3DL2ToNative( &ReverbParameters, &NativeParameters );

		AudioDevice->ValidateAPICall( TEXT( "SetEffectParameters (Reverb)" ), 
			ReverbEffectVoice->SetEffectParameters( 0, &NativeParameters, sizeof( NativeParameters ) ) );
	}
}

/**
 * Applies the generic EQ parameters to the XAudio2 hardware
 */
void FXAudio2EffectsManager::SetEQEffectParameters( const FAudioEQEffect& EQEffectParameters )
{
	FXEQ_PARAMETERS NativeParameters;

	NativeParameters.FrequencyCenter0 = EQEffectParameters.LFFrequency;
	NativeParameters.Gain0 = EQEffectParameters.LFGain;
	NativeParameters.Bandwidth0 = FXEQ_DEFAULT_BANDWIDTH;

	NativeParameters.FrequencyCenter1 = EQEffectParameters.MFCutoffFrequency;
	NativeParameters.Gain1 = EQEffectParameters.MFGain;
	NativeParameters.Bandwidth1 = EQEffectParameters.MFBandwidth;

	NativeParameters.FrequencyCenter2 = EQEffectParameters.HFFrequency;
	NativeParameters.Gain2 = EQEffectParameters.HFGain;
	NativeParameters.Bandwidth2 = FXEQ_DEFAULT_BANDWIDTH;

	NativeParameters.FrequencyCenter3 = FXEQ_DEFAULT_FREQUENCY_CENTER_3;
	NativeParameters.Gain3 = FXEQ_DEFAULT_GAIN;
	NativeParameters.Bandwidth3 = FXEQ_DEFAULT_BANDWIDTH;

	AudioDevice->ValidateAPICall( TEXT( "SetEffectParameters (EQ)" ), 
		EQPremasterVoice->SetEffectParameters( 0, &NativeParameters, sizeof( NativeParameters ) ) );
}


