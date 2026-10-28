/*
 * KICK simulator sandbox (Linux)
 *
 * The firmware code assumes it runs on the KICK hardware and calls shell
 * commands that reconfigure the whole machine: timezone, network interface
 * (ifconfig eth0 192.168.0.1), DHCP, display backlight, mount, firmware
 * decryption, reboot. On a desktop or home server that is dangerous.
 *
 * In the simulator build every such call is intercepted at link time
 * (-Wl,--wrap=...). Nothing is executed except a small allow-list of
 * read-only queries used by the System Resources screen. Blocked calls are
 * logged to stderr with the prefix "[AwesomeKick sandbox]".
 *
 * Set KICKEM_SANDBOX_QUIET=1 to silence the log lines.
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#if defined( __linux__ )

#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <spawn.h>
#include <sys/types.h>
#include <unistd.h>

namespace
{
    bool quiet()
    {
        const char* q = std::getenv( "KICKEM_SANDBOX_QUIET" );
        return q != nullptr && q[0] == '1';
    }

    void logBlocked( const char* what, const char* cmd )
    {
        if( !quiet() )
            std::fprintf( stderr, "[AwesomeKick sandbox] blocked %s: %s\n", what, cmd ? cmd : "(null)" );
    }

    // Read-only commands the simulator may run. A command is allowed only if
    // it starts with one of these prefixes and contains no shell
    // metacharacters, so it cannot be chained with anything else.
    bool isAllowedReadOnly( const char* cmd )
    {
        if( cmd == nullptr )
            return false;

        static const char* const allowedPrefixes[] = { "df -h ", "du -sh " };

        bool prefixOk = false;
        for( const char* p : allowedPrefixes )
            if( std::strncmp( cmd, p, std::strlen( p ) ) == 0 )
                prefixOk = true;

        if( !prefixOk )
            return false;

        return std::strpbrk( cmd, ";&|`$<>\n\\" ) == nullptr;
    }
} // namespace

extern "C"
{
    // Real implementations, provided by the linker for wrapped symbols.
    FILE* __real_popen( const char* command, const char* type );

    int __wrap_system( const char* command )
    {
        if( command == nullptr )
            return 0; // "no shell available"
        logBlocked( "system()", command );
        return -1;
    }

    FILE* __wrap_popen( const char* command, const char* type )
    {
        if( isAllowedReadOnly( command ) && type != nullptr && type[0] == 'r' )
            return __real_popen( command, type );

        logBlocked( "popen()", command );
        errno = EPERM;
        return nullptr;
    }

    int __wrap_execve( const char* path, char* const[], char* const[] )
    {
        logBlocked( "execve()", path );
        errno = EPERM;
        return -1;
    }

    int __wrap_execv( const char* path, char* const[] )
    {
        logBlocked( "execv()", path );
        errno = EPERM;
        return -1;
    }

    int __wrap_execvp( const char* file, char* const[] )
    {
        logBlocked( "execvp()", file );
        errno = EPERM;
        return -1;
    }

    int __wrap_execvpe( const char* file, char* const[], char* const[] )
    {
        logBlocked( "execvpe()", file );
        errno = EPERM;
        return -1;
    }

    int __wrap_execl( const char* path, const char*, ... )
    {
        logBlocked( "execl()", path );
        errno = EPERM;
        return -1;
    }

    int __wrap_execlp( const char* file, const char*, ... )
    {
        logBlocked( "execlp()", file );
        errno = EPERM;
        return -1;
    }

    int __wrap_execle( const char* path, const char*, ... )
    {
        logBlocked( "execle()", path );
        errno = EPERM;
        return -1;
    }

    int __wrap_posix_spawn( pid_t*, const char* path, const posix_spawn_file_actions_t*, const posix_spawnattr_t*, char* const[], char* const[] )
    {
        logBlocked( "posix_spawn()", path );
        return EPERM;
    }

    int __wrap_posix_spawnp( pid_t*, const char* file, const posix_spawn_file_actions_t*, const posix_spawnattr_t*, char* const[], char* const[] )
    {
        logBlocked( "posix_spawnp()", file );
        return EPERM;
    }
}

#endif
