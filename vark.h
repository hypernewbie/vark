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

#pragma once

#include <vector>
#include <string>
#include <cstdint>
#include <cstdio>
#include <filesystem>

#define VARK_PERSISTENT_FP 0x1
#define VARK_PERSISTENT_TEMPBUFFER 0x2
#define VARK_MMAP 0x4
#define VARK_WRITE 0x8

struct VarkFile 
{
    std::filesystem::path path;
    uint64_t offset = 0ull;
    uint64_t size = 0ull;
    uint64_t hash = 0ull;
};

struct Vark
{
    std::filesystem::path path;
    std::vector< VarkFile > files;
    uint64_t size = 0ull;
    FILE* fp = nullptr;
    std::vector< uint8_t > tempBuffer;
    void* mmapHandle = nullptr;
    uint32_t flags = 0;
};

// Create a new archive pointed at give path. This will write a header to the archive file with empty contents.
bool VarkCreateArchive( Vark& vark, const std::string& path, uint32_t flags = 0 );

// Load an existing archive from the given path. This does not load any files, but does load the file table from the archive header.
bool VarkLoadArchive( Vark& vark, const std::string& path, uint32_t flags = 0 );

// Close the archive file if it was opened with persistentFP.
void VarkCloseArchive( Vark& vark );

// Decompress a file from the archive into a vector of bytes.
bool VarkDecompressFile( Vark& vark, const std::string& file, std::vector< uint8_t >& data );

// Compress a file and append it to the archive.
bool VarkCompressAppendFile( Vark& vark, const std::string& file );

#ifdef VARK_IMPLEMENTATION

#define LZAV_NS_CUSTOM lzav
#include "lzav.h"
#include "mio.hpp"
#include <cstring>

#ifdef _WIN32
#define VARK_FSEEK _fseeki64
#define VARK_FTELL _ftelli64
#else
#define VARK_FSEEK fseek
#define VARK_FTELL ftell
#endif

static uint64_t VarkHash( const void* data, size_t size )
{
    const uint8_t* bytes = (const uint8_t*)data;
    uint64_t hash = 14695981039346656037ull;
    for ( size_t i = 0; i < size; ++i )
    {
        hash ^= bytes[i];
        hash *= 1099511628211ull;
    }
    return hash;
}

static void VarkWriteString( FILE* fp, const std::string& s )
{
    uint32_t len = (uint32_t)s.length();
    fwrite( &len, sizeof(len), 1, fp );
    if ( len > 0 ) fwrite( s.c_str(), 1, len, fp );
}

static std::string VarkReadString( FILE* fp )
{
    uint32_t len = 0;
    if ( fread( &len, sizeof(len), 1, fp ) != 1 ) return "";
    if ( len == 0 ) return "";
    std::string s( len, '\0' );
    if ( fread( &s[0], 1, len, fp ) != len ) return "";
    return s;
}

bool VarkCreateArchive( Vark& vark, const std::string& path, uint32_t flags )
{
    FILE* fp = nullptr;
    const char magic[] = "VARK";
    uint64_t tableOffset = 12; // Magic(4) + TableOffset(8)
    uint64_t count = 0;

    if ( ( flags & VARK_WRITE ) && ( flags & VARK_MMAP ) ) return false;

    fp = fopen( path.c_str(), "wb" );
    if ( !fp ) goto error0;

    if ( fwrite( magic, 1, 4, fp ) != 4 ) goto error1;

    uint64_t tableOffset = 12; // Magic(4) + TableOffset(8)
    if ( fwrite( &tableOffset, sizeof(tableOffset), 1, fp ) != 1 ) goto error1;

    uint64_t count = 0;
    if ( fwrite( &count, sizeof(count), 1, fp ) != 1 ) goto error1;

    vark.path = path;
    vark.files.clear();
    vark.size = (uint64_t)VARK_FTELL( fp );
    vark.flags = flags;

    if ( flags & VARK_PERSISTENT_FP )
    {
        vark.fp = fp;
    }
    else
    {
        fclose( fp );
    }
    return true;

error1:
    fclose( fp );
error0:
    return false;
}

bool VarkLoadArchive( Vark& vark, const std::string& path, uint32_t flags )
{
    FILE* fp = nullptr;
    char magic[4];
    uint64_t tableOffset = 0;
    uint64_t count = 0;

    if ( ( flags & VARK_WRITE ) && ( flags & VARK_MMAP ) ) return false;

    fp = fopen( path.c_str(), ( flags & VARK_WRITE ) ? "r+b" : "rb" );
    if ( !fp ) goto error0;

    if ( fread( magic, 1, 4, fp ) != 4 ) goto error1;
    if ( memcmp( magic, "VARK", 4 ) != 0 ) goto error1;

    uint64_t tableOffset = 0;
    if ( fread( &tableOffset, sizeof(tableOffset), 1, fp ) != 1 ) goto error1;

    if ( VARK_FSEEK( fp, (long long)tableOffset, SEEK_SET ) != 0 ) goto error1;

    uint64_t count = 0;
    if ( fread( &count, sizeof(count), 1, fp ) != 1 ) goto error1;

    vark.files.clear();
    vark.files.reserve( (size_t)count );
    vark.path = path;

    for ( uint64_t i = 0; i < count; ++i )
    {
        VarkFile vf;
        vf.path = VarkReadString( fp );
        if ( fread( &vf.offset, sizeof(vf.offset), 1, fp ) != 1 ) goto error1;
        if ( fread( &vf.size, sizeof(vf.size), 1, fp ) != 1 ) goto error1;
        if ( fread( &vf.hash, sizeof(vf.hash), 1, fp ) != 1 ) goto error1;
        vark.files.push_back( vf );
    }

    VARK_FSEEK( fp, 0, SEEK_END );
    vark.size = (uint64_t)VARK_FTELL( fp );
    vark.flags = flags;

    if ( flags & VARK_MMAP )
    {
        std::error_code error;
        mio::mmap_source* mmap = new mio::mmap_source;
        mmap->map( path, error );
        if ( error ) 
        {
            delete mmap;
            goto error1;
        }
        vark.mmapHandle = mmap;
    }

    if ( flags & VARK_PERSISTENT_FP )
    {
        vark.fp = fp;
    }
    else
    {
        fclose( fp );
    }
    return true;

error1:
    fclose( fp );
error0:
    return false;
}

void VarkCloseArchive( Vark& vark )
{
    if ( (vark.flags & VARK_MMAP) && vark.mmapHandle )
    {
        delete (mio::mmap_source*)vark.mmapHandle;
        vark.mmapHandle = nullptr;
    }
    if ( vark.fp ) 
    {
        fclose( vark.fp );
        vark.fp = nullptr;
    }
    if ( vark.flags & VARK_PERSISTENT_TEMPBUFFER )
    {
        vark.tempBuffer.clear();
        vark.tempBuffer.shrink_to_fit();
    }
}

bool VarkDecompressFile( Vark& vark, const std::string& file, std::vector< uint8_t >& data )
{
    const VarkFile* entry = nullptr;
    FILE* fp = nullptr;
    std::vector< uint8_t >* pCompressedData = nullptr;
    std::vector< uint8_t > localCompressedData;
    uint64_t uncompressedSize = 0;
    uint64_t compressedSize = 0;
    bool ownFP = false;

    if ( vark.flags & VARK_WRITE ) return false;

    for ( const auto& f : vark.files )
    {
        if ( f.path == file )
        {
            entry = &f;
            break;
        }
    }

    if ( !entry ) return false;
    if ( entry->size < 8 ) return false; // Must contain uncompressed size header

    compressedSize = entry->size - 8;

    if ( (vark.flags & VARK_MMAP) && vark.mmapHandle )
    {
        mio::mmap_source* mmap = (mio::mmap_source*)vark.mmapHandle;
        if ( !mmap->is_open() ) return false;
        if ( entry->offset + 8 + compressedSize > mmap->size() ) return false;

        const uint8_t* ptr = (const uint8_t*)mmap->data() + entry->offset;
        memcpy( &uncompressedSize, ptr, 8 );
        
        data.resize( (size_t)uncompressedSize );
        if ( uncompressedSize > 0 )
        {
            int res = lzav::lzav_decompress( ptr + 8, data.data(), (int)compressedSize, (int)uncompressedSize );
            if ( res < 0 || (uint64_t)res != uncompressedSize ) return false;
        }
        return true;
    }

    if ( (vark.flags & VARK_PERSISTENT_FP) && vark.fp )
    {
        fp = vark.fp;
    }
    else
    {
        fp = fopen( vark.path.string().c_str(), "rb" );
        if ( !fp ) goto error0;
        ownFP = true;
    }

    if ( VARK_FSEEK( fp, (long long)entry->offset, SEEK_SET ) != 0 ) goto error1;

    if ( fread( &uncompressedSize, sizeof(uncompressedSize), 1, fp ) != 1 ) goto error1;

    compressedSize = entry->size - 8;
    
    if ( vark.flags & VARK_PERSISTENT_TEMPBUFFER )
    {
        pCompressedData = &vark.tempBuffer;
    }
    else
    {
        pCompressedData = &localCompressedData;
    }

    pCompressedData->resize( (size_t)compressedSize );
    
    if ( compressedSize > 0 )
    {
        if ( fread( pCompressedData->data(), 1, (size_t)compressedSize, fp ) != compressedSize ) goto error1;
    }

    if ( ownFP ) fclose( fp );

    data.resize( (size_t)uncompressedSize );
    if ( uncompressedSize > 0 )
    {
        int res = lzav::lzav_decompress( pCompressedData->data(), data.data(), (int)compressedSize, (int)uncompressedSize );
        if ( res < 0 || (uint64_t)res != uncompressedSize ) return false;
    }

    return true;

error1:
    if ( ownFP && fp ) fclose( fp );
error0:
    return false;
}

bool VarkCompressAppendFile( Vark& vark, const std::string& file )
{
    FILE* srcFp = nullptr;
    FILE* fp = nullptr;
    std::vector< uint8_t > srcData;
    std::vector< uint8_t > dstData;
    VarkFile newFile;
    long long srcLen = 0;
    int bound = 0;
    int compressedLen = 0;
    uint64_t tableOffset = 0;
    uint64_t uncompressedSize = 0;
    uint64_t newTableOffset = 0;
    uint64_t count = 0;

    if ( !( vark.flags & VARK_WRITE ) ) return false;

    // Read source file
    srcFp = fopen( file.c_str(), "rb" );
    if ( !srcFp ) goto error0;

    VARK_FSEEK( srcFp, 0, SEEK_END );
    srcLen = VARK_FTELL( srcFp );
    VARK_FSEEK( srcFp, 0, SEEK_SET );

    srcData.resize( (size_t)srcLen );
    if ( srcLen > 0 )
    {
        if ( fread( srcData.data(), 1, (size_t)srcLen, srcFp ) != (size_t)srcLen ) goto error1;
    }
    fclose( srcFp );

    // Compress
    bound = lzav::lzav_compress_bound( (int)srcLen );
    dstData.resize( bound );
    compressedLen = lzav::lzav_compress_default( srcData.data(), dstData.data(), (int)srcLen, bound );
    
    if ( compressedLen == 0 && srcLen > 0 ) return false;

    // Open archive for Read/Update
    if ( (vark.flags & VARK_PERSISTENT_FP) && vark.fp )
    {
        fp = vark.fp;
    }
    else
    {
        fp = fopen( vark.path.string().c_str(), "r+b" );
        if ( !fp ) goto error0;
    }

    // Read existing table offset
    if ( VARK_FSEEK( fp, 4, SEEK_SET ) != 0 ) goto error2;
    if ( fread( &tableOffset, sizeof(tableOffset), 1, fp ) != 1 ) goto error2;

    // Seek to table offset to overwrite it with new file data
    if ( VARK_FSEEK( fp, (long long)tableOffset, SEEK_SET ) != 0 ) goto error2;

    // Write uncompressed size header (8 bytes)
    uncompressedSize = (uint64_t)srcLen;
    if ( fwrite( &uncompressedSize, sizeof(uncompressedSize), 1, fp ) != 1 ) goto error2;

    // Write compressed data
    if ( compressedLen > 0 )
    {
        if ( fwrite( dstData.data(), 1, compressedLen, fp ) != (size_t)compressedLen ) goto error2;
    }

    // Add new entry to in-memory list
    newFile.path = file;
    newFile.offset = tableOffset;
    newFile.size = 8 + compressedLen;
    newFile.hash = VarkHash( srcData.data(), (size_t)srcLen ); 
    vark.files.push_back( newFile );

    // Write new table
    newTableOffset = (uint64_t)VARK_FTELL( fp );
    count = vark.files.size();
    if ( fwrite( &count, sizeof(count), 1, fp ) != 1 ) goto error2;

    for ( const auto& f : vark.files )
    {
        VarkWriteString( fp, f.path.string() );
        fwrite( &f.offset, sizeof(f.offset), 1, fp );
        fwrite( &f.size, sizeof(f.size), 1, fp );
        fwrite( &f.hash, sizeof(f.hash), 1, fp );
    }

    // Update header with new table offset
    if ( VARK_FSEEK( fp, 4, SEEK_SET ) != 0 ) goto error2;
    if ( fwrite( &newTableOffset, sizeof(newTableOffset), 1, fp ) != 1 ) goto error2;

    // Update size in struct
    VARK_FSEEK( fp, 0, SEEK_END );
    vark.size = (uint64_t)VARK_FTELL( fp );

    if ( fp != vark.fp ) fclose( fp );
    return true;

error2:
    if ( fp != vark.fp ) fclose( fp );
    return false;

error1:
    fclose( srcFp );
error0:
    return false;
}

#endif // VARK_IMPLEMENTATION
