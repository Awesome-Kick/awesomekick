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

#include "kick_Resorces.h"

#if( defined( _WIN32 ) || defined( _WIN64 ) )
// The few kernel32 calls used below, declared here: <windows.h> would clash with this unity
// build (its 'interface' macro vs. parameter names in kick_Hwpref.cpp, for one).
namespace mt::win
{
    struct FileTime
    {
        unsigned long low, high;
    };
    struct MemoryStatusEx
    {
        unsigned long      length, memoryLoad;
        unsigned long long totalPhys, availPhys, totalPageFile, availPageFile, totalVirtual, availVirtual, availExtendedVirtual;
    };
    struct ProcessMemoryCounters
    {
        unsigned long cb, pageFaultCount;
        size_t        peakWorkingSetSize, workingSetSize, quotaPeakPagedPoolUsage, quotaPagedPoolUsage, quotaPeakNonPagedPoolUsage, quotaNonPagedPoolUsage,
            pagefileUsage, peakPagefileUsage;
    };
    inline unsigned long long ticks( const FileTime& t ) { return ( (unsigned long long)t.high << 32 ) | t.low; }
}

extern "C"
{
    __declspec( dllimport ) int __stdcall GetSystemTimes( mt::win::FileTime*, mt::win::FileTime*, mt::win::FileTime* );
    __declspec( dllimport ) void* __stdcall GetCurrentProcess();
    __declspec( dllimport ) int __stdcall GetProcessTimes( void*, mt::win::FileTime*, mt::win::FileTime*, mt::win::FileTime*, mt::win::FileTime* );
    __declspec( dllimport ) int __stdcall GlobalMemoryStatusEx( mt::win::MemoryStatusEx* );
    __declspec( dllimport ) int __stdcall K32GetProcessMemoryInfo( void*, mt::win::ProcessMemoryCounters*, unsigned long );
}
#endif

namespace mt
{
    void SystemResource::cpuInfo( double& cpuUsage )
    {
        // Busy share of all cores since the previous call (the first call covers time since boot).
        static unsigned long long prevTotal = 0, prevIdle = 0;
        unsigned long long        total = 0, idle = 0;
#if( JUCE_LINUX || __linux__ )
        std::ifstream      statFile( "/proc/stat" );
        std::string        cpuLabel;
        unsigned long long user = 0, nice = 0, system = 0, idleT = 0, iowait = 0, irq = 0, softirq = 0, steal = 0;
        statFile >> cpuLabel >> user >> nice >> system >> idleT >> iowait >> irq >> softirq >> steal;
        idle  = idleT + iowait;
        total = user + nice + system + idleT + iowait + irq + softirq + steal; // guest time is already in user
#elif( defined( _WIN32 ) || defined( _WIN64 ) )
        win::FileTime idleT, kernel, user;
        if( GetSystemTimes( &idleT, &kernel, &user ) )
        {
            idle  = win::ticks( idleT );
            total = win::ticks( kernel ) + win::ticks( user ); // kernel time includes idle
        }
#endif
        const auto dTotal = total - prevTotal, dIdle = idle - prevIdle;
        cpuUsage          = ( total > prevTotal ) ? 100.0 * (double)( dTotal - dIdle ) / (double)dTotal : 0.0;
        prevTotal         = total;
        prevIdle          = idle;
    }

    void SystemResource::appInfo( sysAppInfo& info )
    {
        // CPU time used by this process since the previous call, as a share of all cores.
        static double prevCpuSeconds = -1.0, prevWallSeconds = 0.0;
        const double  wallSeconds    = juce::Time::getMillisecondCounterHiRes() / 1000.0;
        double        cpuSeconds     = 0.0;
#if( JUCE_LINUX || __linux__ )
        {
            std::ifstream      statm( "/proc/self/statm" );
            unsigned long long sizePages = 0, residentPages = 0;
            statm >> sizePages >> residentPages;
            info.ramMB = (double)residentPages * (double)sysconf( _SC_PAGESIZE ) / ( 1024.0 * 1024.0 );

            // utime and stime are fields 14 and 15; skip past the command name, which may contain spaces.
            std::ifstream stat( "/proc/self/stat" );
            std::string   line;
            std::getline( stat, line );
            std::istringstream fields( line.substr( line.rfind( ')' ) + 2 ) );
            std::string        field;
            unsigned long long utime = 0, stime = 0;
            for( int i = 3; i <= 15 && fields >> field; ++i )
            {
                if( i == 14 )
                    utime = std::stoull( field );
                if( i == 15 )
                    stime = std::stoull( field );
            }
            cpuSeconds = (double)( utime + stime ) / (double)sysconf( _SC_CLK_TCK );
        }
#elif( defined( _WIN32 ) || defined( _WIN64 ) )
        {
            win::ProcessMemoryCounters pmc {};
            pmc.cb = sizeof( pmc );
            if( K32GetProcessMemoryInfo( GetCurrentProcess(), &pmc, sizeof( pmc ) ) )
                info.ramMB = (double)pmc.workingSetSize / ( 1024.0 * 1024.0 );

            win::FileTime created, exited, kernel, user;
            if( GetProcessTimes( GetCurrentProcess(), &created, &exited, &kernel, &user ) )
                cpuSeconds = (double)( win::ticks( kernel ) + win::ticks( user ) ) / 1.0e7; // 100 ns units
        }
#endif
        const double wall = wallSeconds - prevWallSeconds;
        info.cpu          = ( prevCpuSeconds >= 0.0 && wall > 0.0 )
                                ? juce::jlimit( 0.0, 100.0, 100.0 * ( cpuSeconds - prevCpuSeconds ) / ( wall * juce::SystemStats::getNumCpus() ) )
                                : 0.0;
        prevCpuSeconds  = cpuSeconds;
        prevWallSeconds = wallSeconds;
    }

    void SystemResource::sizeInfo( std::string& path, sysSizeInfo& info )
    {
        info.status = false;
#if( JUCE_LINUX || __linux__ )
        juce::File fileToCheck( path );
        if( fileToCheck.exists() )
        {
            info.status          = true;
            juce::String command = "df -h ";
            command += juce::String( path.c_str() ); // Convert std::string to juce::Stringpath;

            // Execute the `df -h` command and capture its output
            FILE* pipe = popen( command.toRawUTF8(), "r" );
            if( !pipe )
            {
                std::cerr << "Error executing command." << std::endl;
                return;
            }

            std::string line;
            char        buffer[256];
            std::string filesystem, size, used, available, usePercentage, mountPoint;
            while( fgets( buffer, sizeof( buffer ), pipe ) )
            {
                std::string line( buffer );
                if( line.find( path ) != std::string::npos )
                {
                    std::istringstream iss( line );
                    iss >> filesystem >> size >> used >> available >> usePercentage >> mountPoint;
                    DBG( " " << filesystem << "Size -> " << size << "Used" << used << "Availd" << available << "Used Per" << usePercentage );
                    info.avaSize   = juce::String( available ).getIntValue();
                    info.usedSize  = juce::String( used ).getIntValue();
                    info.totalSize = juce::String( size ).getIntValue();
                    info.perSize   = juce::String( usePercentage ).getIntValue();
                }
            }
            // Close the command output pipe
            pclose( pipe );
        }
#else
        // The volume holding Path, if it exists here (device paths such as /dev/sda1 don't on a desktop).
        const juce::String name( path.c_str() );
        if( juce::File::isAbsolutePath( name ) && juce::File( name ).exists() )
        {
            const juce::File dir( name );
            const double     mb    = 1024.0 * 1024.0;
            const auto       total = dir.getVolumeTotalSize();
            const auto       free  = dir.getBytesFreeOnVolume();
            if( total > 0 )
            {
                info.status    = true;
                info.totalSize = (double)total / mb;
                info.avaSize   = (double)free / mb;
                info.usedSize  = (double)( total - free ) / mb;
                info.perSize   = (int)( 100 * ( total - free ) / total );
            }
        }
#endif
    }

    void SystemResource::ramInfo( sysRamInfo& info )
    {
        info = {};
#if( JUCE_LINUX || __linux__ )
        // MemAvailable counts reclaimable cache as free; sysinfo()'s freeram does not.
        std::ifstream      meminfo( "/proc/meminfo" );
        std::string        key, unit;
        unsigned long long value = 0, totalKB = 0, availableKB = 0;
        while( meminfo >> key >> value >> unit )
        {
            if( key == "MemTotal:" )
                totalKB = value;
            else if( key == "MemAvailable:" )
                availableKB = value;
        }
        info.totalRam = totalKB / 1024.0;
        info.freeRam  = availableKB / 1024.0;
#elif( defined( _WIN32 ) || defined( _WIN64 ) )
        win::MemoryStatusEx status {};
        status.length = sizeof( status );
        if( GlobalMemoryStatusEx( &status ) )
        {
            info.totalRam = status.totalPhys / ( 1024.0 * 1024.0 );
            info.freeRam  = status.availPhys / ( 1024.0 * 1024.0 );
        }
#endif
        info.usedRam = info.totalRam - info.freeRam;
        info.perRam  = info.totalRam > 0 ? (int)( info.usedRam * 100 / info.totalRam ) : 0;
    }

    void SystemResource::folderSize( std::string path )
    {
#if( JUCE_LINUX || __linux__ )
        const char* folderPath = path.c_str();

        // Create the command to run du -sh on the specified folder
        char command[256];
        snprintf( command, sizeof( command ), "du -sh \"%s\"", folderPath );

        // Open a pipe to execute the command and capture its output
        FILE* pipe = popen( command, "r" );
        if( !pipe )
        {
            std::cerr << "Error executing du command." << std::endl;
            return;
        }

        // Read the output of the du command
        char        buffer[128];
        std::string result;
        while( fgets( buffer, sizeof( buffer ), pipe ) != nullptr )
        {
            result += buffer;
        }

        // Parse the result to extract the size
        char sizeStr[16];
        if( sscanf( result.c_str(), "%s", sizeStr ) == 1 )
        {
            std::cout << "Size of folder '" << folderPath << "': " << sizeStr << std::endl;
        }
        else
        {
            std::cerr << "Error parsing du command output." << std::endl;
            pclose( pipe );
            return;
        }
        pclose( pipe );
#endif
    }
}
