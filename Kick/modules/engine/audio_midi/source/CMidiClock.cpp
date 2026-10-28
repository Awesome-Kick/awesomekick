/*
 * Copyright (c) 2021 MUSIC Tribe
 *
 * This file is part of Kick.
 *
 * Kick is free software: you can redistribute it and/or modify it under the
 * terms of the GNU Affero General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version.
 *
 * Kick is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for
 * more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with Kick. If not, see <https://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "CMidiClock.h"

namespace mt
{

    double CMidiClock::sTickCounter   = 0;
    double CMidiClock::sTickFactor    = 1.0;
    bool   CMidiClock::isTimerRunning = false;

    //######################################
    // Global MIDI Clock Instance
    //######################################
    CMidiClock& getMidiClock() noexcept
    {
        static CMidiClock gMidiClock{};
        return gMidiClock;
    }

    //######################################
    // Platform specific timer setup/control
    //######################################
    inline void CMidiClock::initPlatformTimer()
    { //
    }

    inline void CMidiClock::stopPlatformTimer()
    { //
        if( isTimerRunning )
        {
            getMidiClock().stopTimer();
            isTimerRunning = false;
        }
    }

    inline void CMidiClock::startPlatformTimer( [[maybe_unused]] uint32_t microSeconds )
    {
        setPlatformTimerInterval( microSeconds );
        getMidiClock().startTimer( 1 );
        isTimerRunning = true;
    }

    inline void CMidiClock::setPlatformTimerInterval( [[maybe_unused]] float microSeconds )
    { // Compute tick multiplier if desired interval is not possible to meet on the platform.
        uint32_t mills = 1;
        sTickFactor    = ( (float)( mills * 1000 ) / (float)microSeconds ) * getMidiClock().getInternalPPQN();
    }

    inline void CMidiClock::handlePlatformTimer()
    { // send tick to interval clock.
        getMidiClock().tickInternal();
    }

    //######################################
    // Call CMidiClock
    //######################################
    CMidiClock::CMidiClock( PPQNResolution PPQN )
    {
        setInternalPPQN( PPQN );
        init();
    }

    CMidiClock::~CMidiClock()
    {
        stop();
    }

    // Clock control API's
    void CMidiClock::init() noexcept
    {
        initPlatformTimer();
        setTempo( tempo.getCurrentValue() );
    }

    void CMidiClock::start( bool ClearCounters ) noexcept
    {
        stopPlatformTimer();
        const juce::ScopedLock guard(clockLock);
        clockCounter       = 0;
        intClockSyncPulses = 0;

        if( ClearCounters )
            resetCounters();

        if( isExternalClockSync() ) resetExternalClockCounters();

        state = isInternalClockSync() ? ClockState::STARTED : ClockState::STARTING;
        onClockStart();

        prevTime = juce::Time::getMillisecondCounterHiRes();
        if(isInternalClockSync()) startPlatformTimer( microsPerQuarterNote );
    }

    void CMidiClock::stop() noexcept
    {
        stopPlatformTimer();

        state = ClockState::STOPPED;
        onClockStop();
    }

    // PPQN factors...
    void CMidiClock::setInternalPPQN( PPQNResolution resolution )
    {
        stop();
        if(int(resolution) <= 0) return;
        intPPQN = resolution;
        recomputePPQNFactor();
    }

    void CMidiClock::setExternalPPQN( PPQNResolution resolution )
    {
        stop();
        if(int(resolution) <= 0) return;
        extPPQN = resolution;
        recomputePPQNFactor();
    }

    // Tempo API's
    float CMidiClock::getTempo()
    {
        const juce::ScopedLock guard(clockLock);
        if( isExternalClockSync() )
        {
            auto averageMicros = averageOfExternalIntervals();
            return ( 0 == averageMicros ) ? tempo.getCurrentValue() : microsToTempo( averageMicros );
        }
        return tempo.getCurrentValue();
    }

    void CMidiClock::setTempo( float Bpm, double RampLengthInSeconds )
    {
        if(!std::isfinite(Bpm) || Bpm <= 0.f) return;
        const juce::ScopedLock guard(clockLock);
        auto numSteps = isInternalClockSync() ? ( RampLengthInSeconds / 0.001 ) : 1;
        tempo.reset( numSteps );
        tempo.setTargetValue( Bpm );
        microsPerQuarterNote = tempoToMicros( Bpm ); // In Micro-Seconds.

        if( isRunning() && isInternalClockSync() )
            setPlatformTimerInterval( microsPerQuarterNote );
    }

    void CMidiClock::setTempoByTime( uint32_t MicrosPerQuarterNote )
    {
        if( 0 == MicrosPerQuarterNote )
            return;

        auto targetTempo = microsToTempo( MicrosPerQuarterNote );
        setTempo( targetTempo );
    }

    // Normally expected to be called at 24PPQN
    void CMidiClock::setClockSyncMode(SyncMode mode) noexcept
    {
        if(syncMode.load() == mode) return;
        stop();
        const juce::ScopedLock guard(clockLock);
        syncMode.store(mode);
        resetExternalClockCounters();
    }

    void CMidiClock::tickExternal(double receivedAtMilliseconds)
    {
        if(!isExternalClockSync() || isStopped()) return;
        const juce::ScopedLock guard(clockLock);
        const auto now = receivedAtMilliseconds > 0.0 && std::isfinite(receivedAtMilliseconds) ? receivedAtMilliseconds : juce::Time::getMillisecondCounterHiRes();
        if(state.load() == ClockState::STARTING)
        {
            state.store(ClockState::STARTED);
            ts = now;
        }
        else
        {
            const auto interval = (now - ts) * 1000.0;
            ts = now;
            if(interval > 0.0 && interval < 5000000.0)
            {
                extIntervals[extIntervalIdx] = (uint32_t)interval;
                extIntervalIdx = (extIntervalIdx + 1) % MAX_EXT_INTERVALS;
                const auto quarter = averageOfExternalIntervals();
                if(quarter) setTempoByTime(quarter);
            }
        }
        ++extClockSyncPulses;
        sTickCounter += double(intPPQN) / double(extPPQN);
        onTick((uint32_t)sTickCounter);
        onSync24Callback(extClockSyncPulses, microsPerQuarterNote);
    }

    void CMidiClock::tickInternal()
    {
        if(isStopped() || isExternalClockSync()) return;
        const juce::ScopedLock guard(clockLock);
        if( tempo.isSmoothing() )
        {
            microsPerQuarterNote = static_cast<uint32_t>( tempoToMicros( tempo.getNextValue() ) ); // In Micro-Seconds.
            setPlatformTimerInterval( microsPerQuarterNote );
        }

        auto currTime  = juce::Time::getMillisecondCounterHiRes();
        auto diffTime  = ( currTime - prevTime );
        auto diffTicks = ( sTickFactor * diffTime );

        if( diffTicks < 1.0 )
            return;
        else
            prevTime = currTime;

        sTickCounter += diffTicks;

        //sTickCounter += sTickFactor;
        //if( sTickCounter < 1.0 )
        //    return;
        //sTickCounter = sTickCounter - 1.0;

        if( ++clockCounter >= intClockPulsesPerExtClockPulse )
        {
            clockCounter = 0;
            // increase internal clock pulses at tempo synchronization rate as per extPPQN.
            ++intClockSyncPulses;

            if( syncTempo && isExternalClockSync() )
            {
                syncTempo = false;

                // sync tick position with external tick clock
                if( ( intClockSyncPulses < extClockSyncPulses ) || ( intClockSyncPulses > ( extClockSyncPulses + 1 ) ) )
                {
                    intClockSyncPulses = extClockSyncPulses;
                    ticks              = extClockSyncPulses * intClockPulsesPerExtClockPulse;
                    clockCounter       = ticks % intClockPulsesPerExtClockPulse;
                }

                // Synchronize tempo
                uint32_t avgIntervalInMicros = averageOfExternalIntervals();
                setTempoByTime( avgIntervalInMicros );
            }

            if( ++syncCounter >= (uint32_t)extPPQN )
            {
                syncCounter = 0;
                onSync24Callback( intClockSyncPulses, microsPerQuarterNote );
            }
        }

        //// tick me
        //++ticks;

        // Callbacks
        onTick( (uint32_t)sTickCounter );
    }

    void CMidiClock::resetCounters()
    {
        sTickCounter = 0;
        ticks        = 0;

        syncTempo   = false;
        syncCounter = 0;
    }

    void CMidiClock::resetExternalClockCounters()
    {
        extClockFirstInterval = false;
        extCounter            = 0;
        extClockSyncPulses    = 0;
        extIntervalIdx        = 0;
        std::fill( extIntervals.begin(), extIntervals.end(), 0 );
    }

    uint32_t CMidiClock::averageOfExternalIntervals() const noexcept
    {
        uint32_t count   = 0;
        uint32_t average = 0;
        for( const auto& val : extIntervals )
        {
            if( val != 0 )
            {
                average += val;
                ++count;
            }
        }
        return count ? uint32_t((uint64_t(average) * uint64_t(extPPQN)) / count) : 0;
    }
}
