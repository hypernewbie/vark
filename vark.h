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
#include <unordered_map>

// ---------------------------------------------------- Interface ----------------------------------------------------

#define VARK_PERSISTENT_FP 0x1
#define VARK_MMAP 0x2
#define VARK_WRITE 0x4

// VarkCompressAppendFile flags
#define VARK_COMPRESS_SHARDED 0x1
#define VARK_DEFAULT_SHARD_SIZE (128 * 1024)

struct VarkFile 
{
    std::filesystem::path path;
    uint64_t offset = 0ull;
    uint64_t size = 0ull;
    uint64_t hash = 0ull;
    uint32_t shardSize = 0;
};

struct Vark
{
    std::filesystem::path path;
    std::vector< VarkFile > files;
    std::unordered_map< std::string, size_t > fileLookup;
    uint64_t size = 0ull;
    FILE* fp = nullptr;
    std::vector< uint8_t > tempBuffer;
    std::vector< uint64_t > tempShardBuffer;
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

// Partially decompress a sharded file from the archive into a vector of bytes. Returns false if the file is not sharded.
bool VarkDecompressFileSharded( Vark& vark, const std::string& file, uint64_t offset, uint64_t size, std::vector< uint8_t >& data );

// Get the uncompressed size of a file in the archive without decompressing it.
bool VarkFileSize( Vark& vark, const std::string& file, uint64_t& outSize );

// Compress a file and append it to the archive.
// flags: VARK_COMPRESS_SHARDED (0x1) - Experimental sharded compression
bool VarkCompressAppendFile( Vark& vark, const std::string& file, uint32_t flags = 0 );

// ---------------------------------------------------- Implementation ----------------------------------------------------

#ifdef VARK_IMPLEMENTATION

#define LZAV_NS_CUSTOM lzav
#include "lzav.h"
#include "mio.hpp"
#include <cstring>
#include <algorithm> // for std::min

#ifdef _WIN32
#define VARK_FSEEK _fseeki64
#define VARK_FTELL _ftelli64
#else
#define VARK_FSEEK fseek
#define VARK_FTELL ftell
#endif

struct VarkShardInfo 
{
    uint32_t shardCount;
    uint64_t totalUncompressedSize;
    const uint64_t* pOffsets;      // pointer into mmap OR tempShardBuffer
    const uint8_t* pCompressedData; // pointer to start of compressed chunks (mmap only, nullptr for FP)
};

// Simple RAII for conditional FILE* ownership
struct VarkFP {
    FILE* fp = nullptr;
    bool owns = false;
    VarkFP() = default;
    VarkFP( FILE* f, bool own ) : fp(f), owns(own) {}
    ~VarkFP() { if ( owns && fp ) fclose( fp ); }
    operator FILE*() const { return fp; }
    explicit operator bool() const { return fp != nullptr; }
};

// Acquires read FP from vark. Returns empty handle if MMAP mode.
static VarkFP VarkAcquireReadFP( Vark& vark )
{
    if ( (vark.flags & VARK_MMAP) && vark.mmapHandle ) return {};
    if ( (vark.flags & VARK_PERSISTENT_FP) && vark.fp ) return { vark.fp, false };
    return { fopen( vark.path.string().c_str(), "rb" ), true };
}

// Acquires read/write FP from vark for append operations.
static VarkFP VarkAcquireWriteFP( Vark& vark )
{
    if ( (vark.flags & VARK_PERSISTENT_FP) && vark.fp ) return { vark.fp, false };
    return { fopen( vark.path.string().c_str(), "r+b" ), true };
}

// For MMAP: parses VSHF from memory, returns header. pOffsets points into mmap via tempShardBuffer copy.
static bool VarkParseVSHF_MMAP( Vark& vark, const uint8_t* ptr, size_t entrySize, VarkShardInfo& out )
{
    if ( entrySize < 16 ) return false; // minimum: magic(4) + shardCount(4) + totalSize(8)
    if ( memcmp( ptr, "VSHF", 4 ) != 0 ) return false;
    ptr += 4;
    memcpy( &out.shardCount, ptr, 4 ); ptr += 4;
    memcpy( &out.totalUncompressedSize, ptr, 8 ); ptr += 8;
    size_t needed = 16 + (size_t)(out.shardCount + 1) * 8;
    if ( entrySize < needed ) return false;
    vark.tempShardBuffer.resize( out.shardCount + 1 );
    memcpy( vark.tempShardBuffer.data(), ptr, (out.shardCount + 1) * 8 );
    out.pOffsets = vark.tempShardBuffer.data();
    out.pCompressedData = ptr + (out.shardCount + 1) * 8;
    return true;
}

// For FP: parses VSHF from file, leaves fp positioned after offsets array.
static bool VarkParseVSHF_Fp( Vark& vark, FILE* fp, VarkShardInfo& out )
{
    char vshf[4];
    if ( fread( vshf, 1, 4, fp ) != 4 || memcmp( vshf, "VSHF", 4 ) != 0 ) return false;
    if ( fread( &out.shardCount, sizeof(out.shardCount), 1, fp ) != 1 ) return false;
    if ( fread( &out.totalUncompressedSize, sizeof(out.totalUncompressedSize), 1, fp ) != 1 ) return false;
    vark.tempShardBuffer.resize( out.shardCount + 1 );
    if ( fread( vark.tempShardBuffer.data(), sizeof(uint64_t), out.shardCount + 1, fp ) != out.shardCount + 1 ) return false;
    out.pOffsets = vark.tempShardBuffer.data();
    out.pCompressedData = nullptr; // FP path reads chunks individually
    return true;
}

// Decompresses range [offset, offset+size) from sharded data into `data`.
// For MMAP: hdr.pCompressedData must be set, fp should be nullptr.
// For FP: hdr.pCompressedData should be nullptr, reads via fp from dataStartPos.
static bool VarkDecompressShards(
    Vark& vark,
    FILE* fp,                           // nullptr for MMAP
    const VarkShardInfo& hdr,
    uint32_t shardSize,
    uint64_t dataStartPos,              // file position of compressed data (FP only)
    uint64_t offset,                    // byte offset into uncompressed data (0 for full)
    uint64_t size,                      // bytes to decompress (totalUncompressedSize for full)
    std::vector<uint8_t>& data
)
{
    if ( size == 0 ) { data.clear(); return true; }

    uint32_t firstShard = (uint32_t)(offset / shardSize);
    uint32_t lastShard = (uint32_t)((offset + size - 1) / shardSize);
    uint64_t sliceOffset = offset - (uint64_t)firstShard * shardSize;
    uint64_t firstShardStart = (uint64_t)firstShard * shardSize;
    uint64_t lastShardEnd = (std::min)((uint64_t)(lastShard + 1) * shardSize, hdr.totalUncompressedSize);
    uint64_t oversizedLen = lastShardEnd - firstShardStart;

    data.resize( (size_t)oversizedLen );

    for ( uint32_t i = firstShard; i <= lastShard; ++i )
    {
        uint64_t shardStart = (uint64_t)i * shardSize;
        uint64_t shardEnd = (std::min)(shardStart + shardSize, hdr.totalUncompressedSize);
        uint64_t cSize = hdr.pOffsets[i + 1] - hdr.pOffsets[i];
        uint64_t uSize = shardEnd - shardStart;
        uint8_t* dst = data.data() + (i - firstShard) * shardSize;

        const uint8_t* src;
        if ( hdr.pCompressedData )
        {
            src = hdr.pCompressedData + hdr.pOffsets[i];
        }
        else
        {
            if ( VARK_FSEEK( fp, (long long)(dataStartPos + hdr.pOffsets[i]), SEEK_SET ) != 0 ) return false;
            vark.tempBuffer.resize( (size_t)cSize );
            if ( fread( vark.tempBuffer.data(), 1, vark.tempBuffer.size(), fp ) != cSize ) return false;
            src = vark.tempBuffer.data();
        }

        int res = lzav::lzav_decompress( src, dst, (int)cSize, (int)uSize );
        if ( res < 0 || (uint64_t)res != uSize ) return false;
    }

    if ( sliceOffset > 0 )
    {
        memmove( data.data(), data.data() + sliceOffset, (size_t)size );
    }
    data.resize( (size_t)size );

    return true;
}

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

    fp = fopen( path.c_str(), "w+b" );
    if ( !fp ) goto error0;

    if ( fwrite( magic, 1, 4, fp ) != 4 ) goto error1;
    if ( fwrite( &tableOffset, sizeof(tableOffset), 1, fp ) != 1 ) goto error1;
    if ( fwrite( &count, sizeof(count), 1, fp ) != 1 ) goto error1;

    vark.path = path;
    vark.files.clear();
    vark.fileLookup.clear();
    vark.size = (uint64_t)VARK_FTELL( fp );
    vark.flags = flags;

    if ( flags & VARK_PERSISTENT_FP ) vark.fp = fp;
    else fclose( fp );
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

    if ( fread( &tableOffset, sizeof(tableOffset), 1, fp ) != 1 ) goto error1;
    if ( VARK_FSEEK( fp, (long long)tableOffset, SEEK_SET ) != 0 ) goto error1;
    if ( fread( &count, sizeof(count), 1, fp ) != 1 ) goto error1;

    vark.files.clear();
    vark.fileLookup.clear();
    vark.files.reserve( (size_t)count );
    vark.path = path;

    for ( uint64_t i = 0; i < count; ++i )
    {
        VarkFile vf;
        vf.path = VarkReadString( fp );
        if ( fread( &vf.offset, sizeof(vf.offset), 1, fp ) != 1 ) goto error1;
        if ( fread( &vf.size, sizeof(vf.size), 1, fp ) != 1 ) goto error1;
        if ( fread( &vf.hash, sizeof(vf.hash), 1, fp ) != 1 ) goto error1;
        
        vark.fileLookup[vf.path.generic_string<char>()] = vark.files.size();
        vark.files.push_back( vf );
    }

    if ( count > 0 )
    {
        char vshdMagic[4];
        if ( fread( vshdMagic, 1, 4, fp ) == 4 && memcmp( vshdMagic, "VSHD", 4 ) == 0 )
        {
             uint64_t shardCount = 0;
             if ( fread( &shardCount, sizeof(shardCount), 1, fp ) == 1 )
             {
                 if ( shardCount == count )
                 {
                     for ( size_t i = 0; i < count; ++i )
                     {
                         fread( &vark.files[i].shardSize, sizeof(uint32_t), 1, fp );
                     }
                 }
             }
        }
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

    if ( flags & VARK_PERSISTENT_FP ) vark.fp = fp;
    else fclose( fp );
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
    
    vark.tempBuffer.clear();
    vark.tempBuffer.shrink_to_fit();
    vark.tempShardBuffer.clear();
    vark.tempShardBuffer.shrink_to_fit();
    vark.fileLookup.clear();
}

bool VarkDecompressFile( Vark& vark, const std::string& file, std::vector< uint8_t >& data )
{
    const VarkFile* entry = nullptr;
    uint64_t uncompressedSize = 0;
    uint64_t compressedSize = 0;

    if ( vark.flags & VARK_WRITE ) return false;

    auto it = vark.fileLookup.find( std::filesystem::path( file ).generic_string<char>() );
    if ( it == vark.fileLookup.end() ) return false;
    entry = &vark.files[it->second];

    if ( entry->shardSize > 0 )
    {
        VarkFP fp = VarkAcquireReadFP( vark );
        mio::mmap_source* mmap = (vark.flags & VARK_MMAP) ? (mio::mmap_source*)vark.mmapHandle : nullptr;
        
        VarkShardInfo hdr;
        uint64_t dataStartPos = 0;
        
        if ( mmap )
        {
            if ( !mmap->is_open() ) return false;
            if ( !VarkParseVSHF_MMAP( vark, (const uint8_t*)mmap->data() + entry->offset, (size_t)entry->size, hdr ) ) return false;
        }
        else
        {
            if ( !fp ) return false;
            if ( VARK_FSEEK( fp, (long long)entry->offset, SEEK_SET ) != 0 ) return false;
            if ( !VarkParseVSHF_Fp( vark, fp, hdr ) ) return false;
            dataStartPos = VARK_FTELL( fp );
        }
        
        return VarkDecompressShards( vark, fp, hdr, entry->shardSize, dataStartPos, 0, hdr.totalUncompressedSize, data );
    }

    if ( entry->size < 8 ) return false; 
    compressedSize = entry->size - 8;

    VarkFP fp = VarkAcquireReadFP( vark );
    mio::mmap_source* mmap = (vark.flags & VARK_MMAP) ? (mio::mmap_source*)vark.mmapHandle : nullptr;

    if ( mmap )
    {
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

    if ( !fp ) return false;
    if ( VARK_FSEEK( fp, (long long)entry->offset, SEEK_SET ) != 0 ) return false;
    if ( fread( &uncompressedSize, sizeof(uncompressedSize), 1, fp ) != 1 ) return false;

    vark.tempBuffer.resize( (size_t)compressedSize );
    if ( compressedSize > 0 )
    {
        if ( fread( vark.tempBuffer.data(), 1, (size_t)compressedSize, fp ) != compressedSize ) return false;
    }

    data.resize( (size_t)uncompressedSize );
    if ( uncompressedSize > 0 )
    {
        int res = lzav::lzav_decompress( vark.tempBuffer.data(), data.data(), (int)compressedSize, (int)uncompressedSize );
        if ( res < 0 || (uint64_t)res != uncompressedSize ) return false;
    }

    return true;
}

bool VarkDecompressFileSharded( Vark& vark, const std::string& file, uint64_t offset, uint64_t size, std::vector< uint8_t >& data )
{
    if ( vark.flags & VARK_WRITE ) return false;
    if ( size == 0 ) { data.clear(); return true; }

    auto it = vark.fileLookup.find( std::filesystem::path( file ).generic_string<char>() );
    if ( it == vark.fileLookup.end() ) return false;
    const VarkFile* entry = &vark.files[it->second];

    if ( entry->shardSize == 0 ) return false;

    VarkFP fp = VarkAcquireReadFP( vark );
    mio::mmap_source* mmap = (vark.flags & VARK_MMAP) ? (mio::mmap_source*)vark.mmapHandle : nullptr;

    VarkShardInfo hdr;
    uint64_t dataStartPos = 0;

    if ( mmap )
    {
        if ( !mmap->is_open() ) return false;
        if ( !VarkParseVSHF_MMAP( vark, (const uint8_t*)mmap->data() + entry->offset, (size_t)entry->size, hdr ) ) return false;
    }
    else
    {
        if ( !fp ) return false;
        if ( VARK_FSEEK( fp, (long long)entry->offset, SEEK_SET ) != 0 ) return false;
        if ( !VarkParseVSHF_Fp( vark, fp, hdr ) ) return false;
        dataStartPos = VARK_FTELL( fp );
    }

    if ( offset + size > hdr.totalUncompressedSize ) return false;

    return VarkDecompressShards( vark, fp, hdr, entry->shardSize, dataStartPos, offset, size, data );
}

bool VarkFileSize( Vark& vark, const std::string& file, uint64_t& outSize )
{
    if ( vark.flags & VARK_WRITE ) return false;

    auto it = vark.fileLookup.find( std::filesystem::path( file ).generic_string() );
    if ( it == vark.fileLookup.end() ) return false;
    const VarkFile* entry = &vark.files[it->second];

    if ( entry->shardSize > 0 )
    {
        // Sharded File: Header is VSHF(4) + Count(4) + Size(8)
        if ( entry->size < 16 ) return false;

        if ( (vark.flags & VARK_MMAP) && vark.mmapHandle )
        {
             mio::mmap_source* mmap = (mio::mmap_source*)vark.mmapHandle;
             if ( !mmap->is_open() ) return false;
             if ( entry->offset + 16 > mmap->size() ) return false; // Basic bounds check for header

             const uint8_t* ptr = (const uint8_t*)mmap->data() + entry->offset;
             if ( memcmp( ptr, "VSHF", 4 ) != 0 ) return false;
             memcpy( &outSize, ptr + 8, 8 );
             return true;
        }
        else
        {
             VarkFP fp = VarkAcquireReadFP( vark );
             if ( !fp ) return false;
             if ( VARK_FSEEK( fp, (long long)entry->offset, SEEK_SET ) != 0 ) return false;
             
             char magic[4];
             if ( fread( magic, 1, 4, fp ) != 4 ) return false;
             if ( memcmp( magic, "VSHF", 4 ) != 0 ) return false;
             
             uint32_t shardCount;
             if ( fread( &shardCount, sizeof(shardCount), 1, fp ) != 1 ) return false;
             if ( fread( &outSize, sizeof(outSize), 1, fp ) != 1 ) return false;
             return true;
        }
    }
    else
    {
        // Standard File: Header is Size(8)
        if ( entry->size < 8 ) return false;

        if ( (vark.flags & VARK_MMAP) && vark.mmapHandle )
        {
             mio::mmap_source* mmap = (mio::mmap_source*)vark.mmapHandle;
             if ( !mmap->is_open() ) return false;
             if ( entry->offset + 8 > mmap->size() ) return false;

             const uint8_t* ptr = (const uint8_t*)mmap->data() + entry->offset;
             memcpy( &outSize, ptr, 8 );
             return true;
        }
        else
        {
             VarkFP fp = VarkAcquireReadFP( vark );
             if ( !fp ) return false;
             if ( VARK_FSEEK( fp, (long long)entry->offset, SEEK_SET ) != 0 ) return false;
             if ( fread( &outSize, sizeof(outSize), 1, fp ) != 1 ) return false;
             return true;
        }
    }
}

bool VarkCompressAppendFile( Vark& vark, const std::string& file, uint32_t flags )
{
    VarkFile newFile;
    long long srcLen = 0;
    uint64_t tableOffset = 0;
    uint64_t newTableOffset = 0;
    uint64_t count = 0;
    uint64_t uncompressedSize = 0;

    if ( !( vark.flags & VARK_WRITE ) ) return false;

    // Read source file
    std::vector< uint8_t > srcData;
    {
        VarkFP srcFp( fopen( file.c_str(), "rb" ), true );
        if ( !srcFp ) return false;

        VARK_FSEEK( srcFp, 0, SEEK_END );
        srcLen = VARK_FTELL( srcFp );
        VARK_FSEEK( srcFp, 0, SEEK_SET );

        srcData.resize( (size_t)srcLen );
        if ( srcLen > 0 )
        {
            if ( fread( srcData.data(), 1, (size_t)srcLen, srcFp ) != (size_t)srcLen ) return false;
        }
    } // srcFp closes automatically here
    
    VarkFP fp = VarkAcquireWriteFP( vark );
    if ( !fp ) return false;

    if ( VARK_FSEEK( fp, 4, SEEK_SET ) != 0 ) return false;
    if ( fread( &tableOffset, sizeof(tableOffset), 1, fp ) != 1 ) return false;
    if ( VARK_FSEEK( fp, (long long)tableOffset, SEEK_SET ) != 0 ) return false;

    newFile.offset = tableOffset;
    newFile.path = file;
    newFile.hash = VarkHash( srcData.data(), (size_t)srcLen );

    if ( flags & VARK_COMPRESS_SHARDED )
    {
        newFile.shardSize = VARK_DEFAULT_SHARD_SIZE;
        uint32_t shardCount = (uint32_t)((srcLen + newFile.shardSize - 1) / newFile.shardSize);
        if ( srcLen == 0 ) shardCount = 0;

        std::vector< std::vector<uint8_t> > chunks( shardCount );
        vark.tempShardBuffer.resize( shardCount + 1 );
        uint64_t currentOffset = 0;
        vark.tempShardBuffer[0] = 0;

        for ( uint32_t i = 0; i < shardCount; ++i )
        {
            size_t chunkStart = i * newFile.shardSize;
            size_t chunkLen = (std::min)((size_t)newFile.shardSize, (size_t)(srcLen - chunkStart));
            int bound = lzav::lzav_compress_bound( (int)chunkLen );
            chunks[i].resize( bound );
            int cLen = lzav::lzav_compress_default( srcData.data() + chunkStart, chunks[i].data(), (int)chunkLen, bound );
            chunks[i].resize( cLen );
            currentOffset += cLen;
            vark.tempShardBuffer[i + 1] = currentOffset;
        }

        const char vshf[] = "VSHF";
        fwrite( vshf, 1, 4, fp );
        fwrite( &shardCount, sizeof(shardCount), 1, fp );
        uncompressedSize = (uint64_t)srcLen;
        fwrite( &uncompressedSize, sizeof(uncompressedSize), 1, fp );
        fwrite( vark.tempShardBuffer.data(), sizeof(uint64_t), shardCount + 1, fp );

        for ( const auto& chunk : chunks )
        {
            if ( !chunk.empty() ) fwrite( chunk.data(), 1, chunk.size(), fp );
        }

        newFile.size = 4 + 4 + 8 + (shardCount + 1) * 8 + currentOffset;
    }
    else
    {
        newFile.shardSize = 0;
        int bound = lzav::lzav_compress_bound( (int)srcLen );
        std::vector< uint8_t > dstData( bound );
        int compressedLen = lzav::lzav_compress_default( srcData.data(), dstData.data(), (int)srcLen, bound );
        if ( compressedLen == 0 && srcLen > 0 ) return false;

        uncompressedSize = (uint64_t)srcLen;
        if ( fwrite( &uncompressedSize, sizeof(uncompressedSize), 1, fp ) != 1 ) return false;
        if ( compressedLen > 0 )
        {
            if ( fwrite( dstData.data(), 1, compressedLen, fp ) != (size_t)compressedLen ) return false;
        }
        newFile.size = 8 + compressedLen;
    }

    vark.fileLookup[newFile.path.generic_string<char>()] = vark.files.size();
    vark.files.push_back( newFile );

    newTableOffset = (uint64_t)VARK_FTELL( fp );
    count = vark.files.size();
    if ( fwrite( &count, sizeof(count), 1, fp ) != 1 ) return false;

    for ( const auto& f : vark.files )
    {
        VarkWriteString( fp, f.path.generic_string<char>() );
        fwrite( &f.offset, sizeof(f.offset), 1, fp );
        fwrite( &f.size, sizeof(f.size), 1, fp );
        fwrite( &f.hash, sizeof(f.hash), 1, fp );
    }

    const char vshd[] = "VSHD";
    fwrite( vshd, 1, 4, fp );
    fwrite( &count, sizeof(count), 1, fp );
    for ( const auto& f : vark.files ) fwrite( &f.shardSize, sizeof(uint32_t), 1, fp );

    if ( VARK_FSEEK( fp, 4, SEEK_SET ) != 0 ) return false;
    if ( fwrite( &newTableOffset, sizeof(newTableOffset), 1, fp ) != 1 ) return false;

    VARK_FSEEK( fp, 0, SEEK_END );
    vark.size = (uint64_t)VARK_FTELL( fp );

    return true;
}

#endif // VARK_IMPLEMENTATION
