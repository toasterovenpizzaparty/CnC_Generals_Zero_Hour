/*
**	Command & Conquer Generals Zero Hour(tm) - modified version.
**	Licensed under the GNU General Public License v3 or later (see LICENSE.md).
*/
// NullAudioManager.h - an AudioManager that plays nothing.
//
// Used when the game is built without the (proprietary, not included) Miles Sound System SDK.
// Every device level call is a no-op, so the game runs silently; all the game-side audio logic
// in AudioManager still runs.  Replace with a real backend to get sound.

#pragma once
#ifndef NULLAUDIOMANAGER_H
#define NULLAUDIOMANAGER_H

#include "Common/GameAudio.h"

class NullAudioManager : public AudioManager
{
public:
	NullAudioManager() {}
	virtual ~NullAudioManager() {}

#if defined(_DEBUG) || defined(_INTERNAL)
	virtual void audioDebugDisplay(DebugDisplayInterface *dd, void *userData, FILE *fp = NULL ) {}
#endif
	virtual void stopAudio( AudioAffect which ) {}
	virtual void pauseAudio( AudioAffect which ) {}
	virtual void resumeAudio( AudioAffect which ) {}
	virtual void pauseAmbient( Bool shouldPause ) {}
	virtual void killAudioEventImmediately( AudioHandle audioEvent ) {}
	virtual void nextMusicTrack( void ) {}
	virtual void prevMusicTrack( void ) {}
	virtual Bool isMusicPlaying( void ) const { return FALSE; }
	virtual Bool hasMusicTrackCompleted( const AsciiString& trackName, Int numberOfTimes ) const { return FALSE; }
	virtual AsciiString getMusicTrackName( void ) const { return AsciiString::TheEmptyString; }
	virtual void openDevice( void ) {}
	virtual void closeDevice( void ) {}
	virtual void *getDevice( void ) { return NULL; }
	virtual void notifyOfAudioCompletion( UnsignedInt audioCompleted, UnsignedInt flags ) {}
	virtual UnsignedInt getProviderCount( void ) const { return 0; }
	virtual AsciiString getProviderName( UnsignedInt providerNum ) const { return AsciiString::TheEmptyString; }
	virtual UnsignedInt getProviderIndex( AsciiString providerName ) const { return 0; }
	virtual void selectProvider( UnsignedInt providerNdx ) {}
	virtual void unselectProvider( void ) {}
	virtual UnsignedInt getSelectedProvider( void ) const { return 0; }
	virtual void setSpeakerType( UnsignedInt speakerType ) {}
	virtual UnsignedInt getSpeakerType( void ) { return 0; }
	virtual UnsignedInt getNum2DSamples( void ) const { return 0; }
	virtual UnsignedInt getNum3DSamples( void ) const { return 0; }
	virtual UnsignedInt getNumStreams( void ) const { return 0; }
	virtual Bool doesViolateLimit( AudioEventRTS *event ) const { return FALSE; }
	virtual Bool isPlayingLowerPriority( AudioEventRTS *event ) const { return FALSE; }
	virtual Bool isPlayingAlready( AudioEventRTS *event ) const { return FALSE; }
	virtual Bool isObjectPlayingVoice( UnsignedInt objID ) const { return FALSE; }
	virtual void adjustVolumeOfPlayingAudio(AsciiString eventName, Real newVolume) {}
	virtual void removePlayingAudio( AsciiString eventName ) {}
	virtual void removeAllDisabledAudio() {}
	virtual Bool has3DSensitiveStreamsPlaying( void ) const { return FALSE; }
	virtual void *getHandleForBink( void ) { return NULL; }
	virtual void releaseHandleForBink( void ) {}
	virtual void friend_forcePlayAudioEventRTS(const AudioEventRTS* eventToPlay) {}
	virtual void setPreferredProvider(AsciiString providerNdx) {}
	virtual void setPreferredSpeaker(AsciiString speakerType) {}
	virtual Real getFileLengthMS( AsciiString strToLoad ) const { return 0.0f; }
	virtual void closeAnySamplesUsingFile( const void *fileToClose ) {}
	virtual void setDeviceListenerPosition( void ) {}
};

#endif // NULLAUDIOMANAGER_H
