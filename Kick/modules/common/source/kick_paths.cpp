/*
 * Copyright (c) 2022 MUSIC Tribe
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

//common
#define MT_SOURCE_SRCDIR "."

juce::File getAppDirs( DirType T )
{
    auto seprator = juce::File::getSeparatorString();
    // KICK_PREFS_DIR=<folder> keeps preferences elsewhere (used by the simulator self-tests).
    if( T == MTPrefs )
        if( auto dir = juce::SystemStats::getEnvironmentVariable( "KICK_PREFS_DIR", {} ); dir.isNotEmpty() )
            return juce::File( dir ).getFullPathName() + seprator;
#if defined( _WIN32 ) || defined( _WIN64 )
    auto rootP = juce::File::getSpecialLocation( juce::File::currentApplicationFile ).getParentDirectory().getFullPathName() + seprator;
    if( T == MTAssets )
        return juce::File( rootP + juce::String( MT_SOURCE_SRCDIR ) + seprator + "assets" );
    else if( T == MTSamples )
        return juce::File( rootP + juce::String( MT_SOURCE_SRCDIR ) + seprator + "samples" );
    else if( T == MTDemos )
        return juce::File( rootP + juce::String( MT_SOURCE_SRCDIR ) + seprator + "demos" );
    else if( T == MTTemp )
        return juce::File( rootP + juce::String( MT_SOURCE_SRCDIR ) + seprator + "templates" );
    else if( T == MTProjects )
        return juce::File( rootP + juce::String( MT_SOURCE_SRCDIR ) + seprator + "projects" );
    else if( T == MTLog )
        return juce::File( rootP + juce::String( MT_SOURCE_SRCDIR ) + seprator + "logs" + seprator );
    else if( T == MTPrefs )
        return juce::File( rootP + juce::String( MT_SOURCE_SRCDIR ) + seprator + "prefs" + seprator );
#elif defined( __APPLE_CPP__ ) || defined( __APPLE_CC__ ) || defined( JUCE_MAC )
    if( T == MTAssets )
        return juce::File( "./assets" );
    else if( T == MTSamples )
        return juce::File( "./data/samplesKick/" );
    else if( T == MTDemos )
        return juce::File( "./demos" );
    else if( T == MTTemp )
        return juce::File( "./templates" );
    else if( T == MTProjects )
        return juce::File( "./projects" );
    else if( T == MTLog )
        return juce::File( "./logs/" );
    else if( T == MTPrefs )
        return juce::File( "./prefs" );
#elif ( defined( LINUX ) || defined( __linux__ ) ) && defined( SIM_BUILD ) && SIM_BUILD
    // Simulator on a Linux desktop/server: never touch the hardware paths
    // (/app, /data, /home/Kick). Read-only data (assets, demos, templates) is
    // found next to the executable (or a parent folder), or in KICKEM_DATA_DIR.
    // Everything the simulator writes goes to ~/.local/share/KickEM.
    auto findDataRoot = []() -> juce::File
    {
        if( auto dir = juce::SystemStats::getEnvironmentVariable( "KICKEM_DATA_DIR", {} ); dir.isNotEmpty() )
            return juce::File( dir );
        auto exeDir = juce::File::getSpecialLocation( juce::File::currentExecutableFile ).getParentDirectory();
        auto d      = exeDir;
        for( int i = 0; i < 6 && d.exists(); ++i, d = d.getParentDirectory() )
            if( d.getChildFile( "assets" ).isDirectory() )
                return d;
        return exeDir;
    };
    auto userRoot = []( const juce::String& sub ) -> juce::File
    {
        auto f = juce::File::getSpecialLocation( juce::File::userHomeDirectory ).getChildFile( ".local/share/KickEM" ).getChildFile( sub );
        f.createDirectory();
        return f;
    };
    if( T == MTAssets )
        return findDataRoot().getChildFile( "assets" );
    else if( T == MTSamples )
        return userRoot( "samples" );
    else if( T == MTDemos )
        return juce::File( findDataRoot().getChildFile( "demos" ).getFullPathName() + seprator );
    else if( T == MTTemp )
        return juce::File( findDataRoot().getChildFile( "templates" ).getFullPathName() + seprator );
    else if( T == MTProjects )
        return juce::File( userRoot( "projects" ).getFullPathName() + seprator );
    else if( T == MTLog )
        return juce::File( userRoot( "logs" ).getFullPathName() + seprator );
    else if( T == MTPrefs )
        return userRoot( "prefs" );
#elif defined( LINUX ) || defined( __linux__ )
    if( T == MTAssets )
        return juce::File( "/app/assets" );
    else if( T == MTSamples )
        return juce::File( "/data/sounds" );
    else if( T == MTDemos )
        return juce::File( "/app/demos/" );
    else if( T == MTTemp )
        return juce::File( "/app/templates/" );
    else if( T == MTProjects )
        return juce::File( "/home/Kick/projects/" );
    else if( T == MTLog )
        return juce::File( "/home/Kick/logs/" );
    else if( T == MTPrefs )
        return juce::File( "/home/Kick/prefs" );
#endif

    return juce::File::getSpecialLocation( juce::File::tempDirectory );
}

juce::File getDefaultPath()
{
    juce::File rootP;
    auto       seprator = juce::File::getSeparatorString();

#if defined( _WIN32 ) || defined( _WIN64 )
    rootP = juce::File::getSpecialLocation( juce::File::currentApplicationFile ).getParentDirectory().getFullPathName() + seprator;

#elif defined( __APPLE_CPP__ ) || defined( __APPLE_CC__ ) || defined( JUCE_MAC )
    rootP = "./app/";

#elif defined( LINUX ) || defined( __linux__ )
    rootP = "./app/";
#endif
    return rootP;
}
