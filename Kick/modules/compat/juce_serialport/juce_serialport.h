/*******************************************************************************
 BEGIN_JUCE_MODULE_DECLARATION

  ID:                 juce_serialport
  vendor:             community
  version:            0.1.0
  name:               Serial port stub
  description:        Compile-only stand-in for the removed juce_serialport module
  website:
  license:            AGPLv3

  dependencies:       juce_core, juce_events

 END_JUCE_MODULE_DECLARATION
*******************************************************************************/

/*
 * The original juce_serialport was removed from the release (no license).
 * This stub has the same API surface Kick uses but never opens a port, so the
 * front-panel MCU link on /dev/ttymxc0 does nothing. Fine for desktop builds;
 * a device build needs a real (termios) implementation.
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#pragma once

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <functional>

class SerialPortConfig
{
public:
    enum SerialPortParity { SERIALPORT_PARITY_NONE, SERIALPORT_PARITY_ODD, SERIALPORT_PARITY_EVEN, SERIALPORT_PARITY_SPACE, SERIALPORT_PARITY_MARK };
    enum SerialPortStopBits { STOPBITS_1, STOPBITS_1ANDHALF, STOPBITS_2 };
    enum SerialPortFlowControl { FLOWCONTROL_NONE, FLOWCONTROL_HARDWARE, FLOWCONTROL_XONXOFF };

    SerialPortConfig() = default;
    SerialPortConfig( juce::uint32 Bps, juce::uint32 DataBits, SerialPortParity Parity, SerialPortStopBits StopBits, SerialPortFlowControl FlowControl )
        : bps( Bps ), databits( DataBits ), parity( Parity ), stopbits( StopBits ), flowcontrol( FlowControl )
    {
    }

    juce::uint32          bps{ 9600 };
    juce::uint32          databits{ 8 };
    SerialPortParity      parity{ SERIALPORT_PARITY_NONE };
    SerialPortStopBits    stopbits{ STOPBITS_1 };
    SerialPortFlowControl flowcontrol{ FLOWCONTROL_NONE };
};

class SerialPort
{
public:
    using DebugFunction = std::function<void( juce::String, juce::String )>;

    SerialPort( const juce::String&, const SerialPortConfig&, DebugFunction = {} ) {}
    ~SerialPort() = default;

    static juce::StringPairArray getSerialPortPaths() { return {}; }

    bool open( const juce::String& ) { return false; }
    void close() {}
    bool exists() const { return false; }

    JUCE_DECLARE_NON_COPYABLE( SerialPort )
};

class SerialPortInputStream : public juce::InputStream, public juce::ChangeBroadcaster
{
public:
    enum NotifyType { NOTIFY_OFF = 0, NOTIFY_ON_CHAR, NOTIFY_ALWAYS };

    explicit SerialPortInputStream( SerialPort* ) {}

    void setNotify( NotifyType, char = 0 ) {}

    juce::int64 getTotalLength() override { return 0; }
    bool        isExhausted() override { return true; }
    int         read( void*, int ) override { return 0; }
    juce::int64 getPosition() override { return 0; }
    bool        setPosition( juce::int64 ) override { return false; }
};

class SerialPortOutputStream : public juce::OutputStream
{
public:
    explicit SerialPortOutputStream( SerialPort* ) {}

    void        flush() override {}
    bool        setPosition( juce::int64 ) override { return false; }
    juce::int64 getPosition() override { return 0; }
    bool        write( const void*, size_t ) override { return true; }
};
