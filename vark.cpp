/*
    -- Vark --

    Copyright 2026 UAA Software

    Permission is hereby granted, free of charge, to any person obtaining a copy of this software and
    associated documentation files (the "Software"), to deal in the Software without restriction,
    including without limitation the rights to use, copy, modify, merge, publish, distribute,
    sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in all copies or substantial
    portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT
    NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
    NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES
    OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
    CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>
#include <iostream>

#define VARK_IMPLEMENTATION
#include "vark.h"
#include "argparse/argparse.hpp"

namespace fs = std::filesystem;

static void AddToFiles( std::vector<std::string>& files, const std::string& path )
{
    if ( fs::is_directory( path ) )
    {
        for ( const auto& entry : fs::recursive_directory_iterator( path ) )
        {
            if ( entry.is_regular_file() )
            {
                files.push_back( entry.path().string() );
            }
        }
    }
    else if ( fs::is_regular_file( path ) )
    {
        files.push_back( path );
    }
}

static void ProcessFiles( Vark& vark, const std::vector<std::string>& files, const char* actionVerb, uint32_t flags = 0 )
{
    for ( const auto& f : files )
    {
        printf( "  %s: %s\n", actionVerb, f.c_str() );
        if ( !VarkCompressAppendFile( vark, f, flags ) )
        {
            printf( "Error: Failed to %s %s\n", actionVerb, f.c_str() );
        }
    }
}

#ifdef VARK_UNIT_TEST_MODE
int vark_test_main( int argc, char** argv )
#else // !VARK_UNIT_TEST_MODE
int main( int argc, char** argv )
#endif // VARK_UNIT_TEST_MODE
{
    argparse::ArgumentParser program( "vark", "1.04" );

    program.add_description( "A minimal LZAV archive tool for fast compression and decompression. Supports sharded compression for efficient random access to large files. Use without flags for smart mode: extracts existing archives or creates new ones automatically." );

    program.add_argument( "-c" )
        .help( "Create archive" )
        .default_value( false )
        .implicit_value( true );

    program.add_argument( "-cs" )
        .help( "Create archive (sharded compression)" )
        .default_value( false )
        .implicit_value( true );

    program.add_argument( "-a" )
        .help( "Append to archive" )
        .default_value( false )
        .implicit_value( true );

    program.add_argument( "-as" )
        .help( "Append to archive (sharded)" )
        .default_value( false )
        .implicit_value( true );

    program.add_argument( "-x" )
        .help( "Extract archive" )
        .default_value( false )
        .implicit_value( true );

    program.add_argument( "-l" )
        .help( "List archive contents" )
        .default_value( false )
        .implicit_value( true );

    program.add_argument( "-v" )
        .help( "Verify archive integrity" )
        .default_value( false )
        .implicit_value( true );

    program.add_argument( "archive" )
        .help( "archive file path" );

    program.add_argument( "files" )
        .remaining()
        .help( "input files or directories" );

    program.add_epilog( "Examples:\n"
                        "  vark data.vark                    Extract archive (smart mode)\n"
                        "  vark -c game.vark assets/         Create archive from directory\n"
                        "  vark -cs textures.vark images/    Create with sharded compression\n"
                        "  vark -a game.vark newfile.dat     Append file to existing archive\n"
                        "  vark -l game.vark                 List archive contents" );

    try
    {
        program.parse_args( argc, argv );
    }
    catch ( const std::runtime_error& err )
    {
        if ( argc > 1 ) std::cerr << "Error: " << err.what() << std::endl;
        std::cerr << program;
        return 1;
    }

    std::string archivePath = program.get<std::string>( "archive" );
    std::vector<std::string> rawInputFiles;
    try
    {
        rawInputFiles = program.get<std::vector<std::string>>( "files" );
    }
    catch ( ... )
    {
        // No files provided
    }

    std::string mode = "";
    int flagsSet = 0;
    if ( program["-c"]  == true ) { mode = "-c";  flagsSet++; }
    if ( program["-cs"] == true ) { mode = "-cs"; flagsSet++; }
    if ( program["-a"]  == true ) { mode = "-a";  flagsSet++; }
    if ( program["-as"] == true ) { mode = "-as"; flagsSet++; }
    if ( program["-x"]  == true ) { mode = "-x";  flagsSet++; }
    if ( program["-l"]  == true ) { mode = "-l";  flagsSet++; }
    if ( program["-v"]  == true ) { mode = "-v";  flagsSet++; }

    if ( flagsSet > 1 )
    {
        std::cerr << "Error: Multiple mode flags set." << std::endl;
        return 1;
    }

    if ( flagsSet == 0 )
    {
        // Smart mode
        bool exists = fs::exists( archivePath );
        bool hasInputs = !rawInputFiles.empty();

        if ( exists && !hasInputs )
        {
            mode = "-x";
        }
        else if ( exists && hasInputs )
        {
            mode = "-a";
        }
        else
        {
            mode = "-c";
        }
    }

    std::vector<std::string> inputFiles;
    for ( const auto& path : rawInputFiles )
    {
        AddToFiles( inputFiles, path );
    }

    uint32_t compressFlags = 0;
    if ( mode == "-cs" || mode == "-as" ) compressFlags |= VARK_COMPRESS_SHARDED;

    Vark vark;
    if ( mode == "-c" || mode == "-cs" )
    {
        if ( inputFiles.empty() )
        {
            printf( "Error: No input files specified for creation.\n" );
            return 1;
        }

        printf( "Creating archive%s: %s\n", ( mode == "-cs" ? " (sharded)" : "" ), archivePath.c_str() );
        if ( !VarkCreateArchive( vark, archivePath, VARK_WRITE | VARK_PERSISTENT_FP ) )
        {
            printf( "Error: Failed to create archive.\n" );
            return 1;
        }

        ProcessFiles( vark, inputFiles, "Adding", compressFlags );
        VarkCloseArchive( vark );
    }
    else if ( mode == "-a" || mode == "-as" )
    {
        if ( inputFiles.empty() )
        {
            printf( "Error: No input files specified for append.\n" );
            return 1;
        }

        if ( !fs::exists( archivePath ) )
        {
            printf( "Archive not found, creating new: %s\n", archivePath.c_str() );
            if ( !VarkCreateArchive( vark, archivePath, VARK_WRITE | VARK_PERSISTENT_FP ) ) return 1;
        }
        else if ( !VarkLoadArchive( vark, archivePath, VARK_WRITE | VARK_PERSISTENT_FP ) )
        {
            printf( "Error: Failed to load archive %s\n", archivePath.c_str() );
            return 1;
        }

        ProcessFiles( vark, inputFiles, "Appending", compressFlags );
        VarkCloseArchive( vark );
    }
    else if ( mode == "-x" )
    {
        printf( "Extracting archive: %s\n", archivePath.c_str() );
        if ( !VarkLoadArchive( vark, archivePath, VARK_MMAP ) )
        {
            printf( "Error: Failed to load archive.\n" );
            return 1;
        }

        for ( const auto& file : vark.files )
        {
            printf( "  Extracting: %s\n", file.path.string().c_str() );

            std::vector<uint8_t> data;
            if ( VarkDecompressFile( vark, file.path.string(), data ) )
            {
                fs::path parent = file.path.parent_path();
                if ( !parent.empty() ) fs::create_directories( parent );
                
                FILE* out = fopen( file.path.string().c_str(), "wb" );
                if ( out )
                {
                    if ( !data.empty() )
                        fwrite( data.data(), 1, data.size(), out );
                    fclose( out );
                }
                else
                {
                    printf( "    Error: Could not write file.\n" );
                }
            }
            else
            {
                printf( "    Error: Decompression failed.\n" );
            }
        }
        VarkCloseArchive( vark );
    }
    else if ( mode == "-l" )
    {
        if ( !VarkLoadArchive( vark, archivePath ) )
        {
            printf( "Error: Failed to load archive %s\n", archivePath.c_str() );
            return 1;
        }

        printf( "Archive: %s (%llu bytes, %llu files)\n", archivePath.c_str(), vark.size, (uint64_t)vark.files.size() );
        printf( "  Compressed       Uncompressed     Ratio     Sharded   Path\n" );
        printf( "  ---------------  ---------------  --------  --------  -------------\n" );
        for ( const auto& f : vark.files )
        {
            uint64_t uncompressedSize = 0;
            if ( VarkFileSize( vark, f.path.string(), uncompressedSize ) )
            {
                double ratio = ( 1.0 - ( double ) f.size / ( double ) uncompressedSize ) * 100.0;
                printf( "  %15llu  %15llu  %7.1f%%  %8s  %s\n",
                    f.size,
                    uncompressedSize,
                    ratio,
                    ( f.shardSize > 0 ? "\xe2\x9c\x93" : " " ),
                    f.path.string().c_str() );
            }
            else
            {
                printf( "  %15llu  %15s  %7s  %8s  %s\n",
                    f.size,
                    "ERROR",
                    "N/A",
                    ( f.shardSize > 0 ? "\xe2\x9c\x93" : " " ),
                    f.path.string().c_str() );
            }
        }
        VarkCloseArchive( vark );
    }
    else if ( mode == "-v" )
    {
        uint32_t failCount = 0;

        if ( !VarkLoadArchive( vark, archivePath, VARK_MMAP ) )
        {
            printf( "Error: Failed to load archive %s\n", archivePath.c_str() );
            return 1;
        }

        printf( "Verifying archive: %s\n", archivePath.c_str() );
        for ( const auto& file : vark.files )
        {
            std::vector<uint8_t> data;
            uint64_t hash = 0;
            printf( "  %s... ", file.path.string().c_str() );
            if ( VarkDecompressFile( vark, file.path.string(), data ) )
            {
                hash = VarkHash( data.data(), data.size() );
                if ( hash == file.hash )
                {
                    printf( "OK\n" );
                }
                else
                {
                    printf( "FAILED (Hash mismatch)\n" );
                    failCount++;
                }
            }
            else
            {
                printf( "FAILED (Decompression error)\n" );
                failCount++;
            }
        }
        VarkCloseArchive( vark );

        if ( failCount == 0 )
        {
            printf( "\nIntegrity check PASSED.\n" );
        }
        else
        {
            printf( "\nIntegrity check FAILED (%u errors found).\n", failCount );
            return 1;
        }
    }
    else
    {
        std::cerr << program;
        return 1;
    }

    return 0;
}
