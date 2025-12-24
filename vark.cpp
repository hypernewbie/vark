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

void PrintUsage()
{
    printf( "Usage:\n" );
    printf( "  vark -c <archive> <files...>\tCreate archive\n" );
    printf( "  vark -a <archive> <files...>\tAppend to archive\n" );
    printf( "  vark -x <archive>\t\tExtract archive\n" );
    printf( "  vark -l <archive>\t\tList archive contents\n" );
    printf( "  vark -v <archive>\t\tVerify archive integrity\n" );
    printf( "  vark <archive>\t\tSmart mode (Extract if .vark, Create/Append otherwise)\n" );
}

#ifdef VARK_UNIT_TEST_MODE
int vark_test_main( int argc, char** argv )
#else // !VARK_UNIT_TEST_MODE
int main( int argc, char** argv )
#endif // VARK_UNIT_TEST_MODE
{
    std::string mode = "";
    std::string archivePath;
    std::string arg1;
    std::vector<std::string> inputFiles;
    Vark vark;
    int argIdx = 1;
    bool exists = false;
    bool hasInputs = false;

    if ( argc < 2 )
    {
        PrintUsage();
        return 1;
    }

    arg1 = argv[argIdx];

    if ( arg1 == "-c" || arg1 == "-x" || arg1 == "-a" || arg1 == "-l" || arg1 == "-v" )
    {
        mode = arg1;
        argIdx++;
        if ( argIdx >= argc )
        {
            printf( "Error: Missing archive name.\n" );
            return 1;
        }
        archivePath = argv[argIdx++];
    }
    else
    {
        archivePath = arg1;
        argIdx++;

        exists = fs::exists( archivePath );
        hasInputs = ( argIdx < argc );

        if ( exists && !hasInputs )
        {
            mode = "-x";
        }
        else if ( exists && hasInputs )
        {
            mode = "-a";
        }
        else {
            mode = "-c";
        }
    }

    // Gather input files
    for ( ; argIdx < argc; ++argIdx )
    {
        AddToFiles( inputFiles, argv[argIdx] );
    }

    if ( mode == "-c" )
    {
        if ( inputFiles.empty() )
        {
            printf( "Error: No input files specified for creation.\n" );
            return 1;
        }

        printf( "Creating archive: %s\n", archivePath.c_str() );
        if ( !VarkCreateArchive( vark, archivePath, VARK_WRITE | VARK_PERSISTENT_FP ) )
        {
            printf( "Error: Failed to create archive.\n" );
            return 1;
        }

        for ( const auto& f : inputFiles )
        {
            printf( "  Adding: %s\n", f.c_str() );
            if ( !VarkCompressAppendFile( vark, f ) )
            {
                printf( "Error: Failed to add %s\n", f.c_str() );
            }
        }
        VarkCloseArchive( vark );
    }
    else if ( mode == "-a" )
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

        for ( const auto& f : inputFiles )
        {
            printf( "  Appending: %s\n", f.c_str() );
            if ( !VarkCompressAppendFile( vark, f ) )
            {
                printf( "Error: Failed to append %s\n", f.c_str() );
            }
        }
        VarkCloseArchive( vark );
    }
    else if ( mode == "-x" )
    {
        printf( "Extracting archive: %s\n", archivePath.c_str() );
        if ( !VarkLoadArchive( vark, archivePath, VARK_MMAP | VARK_PERSISTENT_TEMPBUFFER ) )
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
                fs::create_directories( file.path.parent_path() );
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
        printf( "  Compressed Size  Original Path\n" );
        printf( "  ---------------  -------------\n" );
        for ( const auto& f : vark.files )
        {
            printf( "  %15llu  %s\n", f.size, f.path.string().c_str() );
        }
    }
    else if ( mode == "-v" )
    {
        uint32_t failCount = 0;

        if ( !VarkLoadArchive( vark, archivePath, VARK_MMAP | VARK_PERSISTENT_TEMPBUFFER ) )
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
        PrintUsage();
        return 1;
    }

    return 0;
}
