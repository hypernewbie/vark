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
#include <vector>
#include <string>
#include <cstdint>
#include <chrono>
#include <filesystem>
#include <cstdlib>

#define VARK_UNIT_TEST_MODE 1
#include "vark.cpp"
#include "utest.h"

// 32-bit hash
uint32_t SimpleHash( const std::vector<uint8_t>& data )
{
    uint32_t hash = 2166136261u;
    for ( uint8_t byte : data ) {
        hash ^= byte;
        hash *= 16777619u;
    }
    return hash;
}

// Helper to read file content
std::vector<uint8_t> ReadFileContent( const std::string& path )
{
    FILE* fp = fopen( path.c_str(), "rb" );
    if ( !fp ) return {};
    fseek( fp, 0, SEEK_END );
    long size = ftell( fp );
    fseek( fp, 0, SEEK_SET );
    std::vector<uint8_t> data( size );
    if ( size > 0 ) {
        fread( data.data(), 1, size, fp );
    }
    fclose( fp );
    return data;
}

const std::vector<std::string> TEST_FILES =
{
    "tests/alice_in_wonderland.txt",
    "tests/soap-bubble-27_jpg_75.jpg",
    "tests/soap-bubble-55_jpg_75.jpg",
    "tests/swoosh_1.wav",
    "tests/Apophysis-250901-101.png",
    "tests/shakespeare.txt",
};

UTEST( Vark, SingleFileTests )
{
    const std::string archivePath = "single_test.vark";

    for ( const auto& filePath : TEST_FILES )
    {
        // Read original file and compute hash
        std::vector<uint8_t> originalData = ReadFileContent( filePath );
        ASSERT_GT( originalData.size(), 0 );
        uint32_t originalHash = SimpleHash( originalData );

        // Create archive and append file
        Vark vark;
        ASSERT_TRUE( VarkCreateArchive( vark, archivePath, VARK_WRITE ) );
        ASSERT_TRUE( VarkCompressAppendFile( vark, filePath ) );
        VarkCloseArchive( vark );

        // Load archive
        Vark varkLoaded;
        ASSERT_TRUE( VarkLoadArchive( varkLoaded, archivePath ) );
        ASSERT_EQ( ( size_t ) 1, varkLoaded.files.size() );

        // Decompress
        std::vector<uint8_t> decompressedData;
        ASSERT_TRUE( VarkDecompressFile( varkLoaded, filePath, decompressedData ) );

        // Verify hash
        uint32_t decompressedHash = SimpleHash( decompressedData );
        ASSERT_EQ( originalHash, decompressedHash );
        ASSERT_EQ( originalData.size(), decompressedData.size() );

        remove( archivePath.c_str() );
    }
}

UTEST( Vark, MultipleFilesTest )
{
    const std::string archivePath = "multi_test.vark";

    // 1. Create archive and append all files
    Vark vark;
    ASSERT_TRUE( VarkCreateArchive( vark, archivePath, VARK_WRITE ) );

    std::vector<uint32_t> originalHashes;
    for ( const auto& filePath : TEST_FILES )
    {
        std::vector<uint8_t> data = ReadFileContent( filePath );
        ASSERT_GT( data.size(), 0 );
        originalHashes.push_back( SimpleHash( data ) );
        ASSERT_TRUE( VarkCompressAppendFile( vark, filePath ) );
    }
    VarkCloseArchive( vark );

    // 2. Load archive
    Vark varkLoaded;
    ASSERT_TRUE( VarkLoadArchive( varkLoaded, archivePath ) );
    ASSERT_EQ( TEST_FILES.size(), varkLoaded.files.size() );

    // 3. Decompress and verify all
    for ( size_t i = 0; i < TEST_FILES.size(); ++i )
    {
        std::vector<uint8_t> decompressedData;
        ASSERT_TRUE( VarkDecompressFile( varkLoaded, TEST_FILES[i], decompressedData ) );

        uint32_t decompressedHash = SimpleHash( decompressedData );
        ASSERT_EQ( originalHashes[i], decompressedHash );
    }

    remove( archivePath.c_str() );
}

UTEST( Vark, PerformanceTest )
{
    const std::string archivePath = "perf_test.vark";
    Vark vark;
    ASSERT_TRUE( VarkCreateArchive( vark, archivePath, VARK_WRITE | VARK_PERSISTENT_FP ) );

    uint64_t totalSizeBytes = 0;
    for ( const auto& filePath : TEST_FILES )
    {
        totalSizeBytes += std::filesystem::file_size( filePath );
    }
    double totalGB = ( double ) totalSizeBytes / ( 1000.0 * 1000.0 * 1000.0 );

    printf( "\n[ Performance Results (Total Size: %.2f MB) ]\n", ( double ) totalSizeBytes / ( 1024.0 * 1024.0 ) );

    // Measure Compression
    auto startComp = std::chrono::high_resolution_clock::now();
    for ( const auto& filePath : TEST_FILES )
    {
        ASSERT_TRUE( VarkCompressAppendFile( vark, filePath ) );
    }
    auto endComp = std::chrono::high_resolution_clock::now();
    VarkCloseArchive( vark );
    std::chrono::duration<double> compTimeSec = endComp - startComp;
    printf( "Compression:   %.2f ms (%.3f GB/sec)\n", compTimeSec.count() * 1000.0, totalGB / compTimeSec.count() );

    Vark varkLoaded;
    ASSERT_TRUE( VarkLoadArchive( varkLoaded, archivePath ) );

    // Measure Decompression
    auto startDecomp = std::chrono::high_resolution_clock::now();
    for ( const auto& filePath : TEST_FILES )
    {
        std::vector<uint8_t> data;
        ASSERT_TRUE( VarkDecompressFile( varkLoaded, filePath, data ) );
    }
    auto endDecomp = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> decompTimeSec = endDecomp - startDecomp;
    printf( "Decompression: %.2f ms (%.3f GB/sec)\n", decompTimeSec.count() * 1000.0, totalGB / decompTimeSec.count() );

    remove( archivePath.c_str() );
}

UTEST( Vark, InMemoryPerf )
{
    printf( "\n[ In-Memory Performance (Raw LZAV) ]\n" );

    // 1. Pre-load all files into memory
    std::vector<std::vector<uint8_t>> sourceFiles;
    uint64_t totalSizeBytes = 0;
    for ( const auto& filePath : TEST_FILES )
    {
        std::vector<uint8_t> data = ReadFileContent( filePath );
        ASSERT_GT( data.size(), 0 );
        sourceFiles.push_back( std::move( data ) );
        totalSizeBytes += sourceFiles.back().size();
    }
    double totalGB = ( double ) totalSizeBytes / ( 1000.0 * 1000.0 * 1000.0 );

    // 2. Pre-allocate compression output buffers
    std::vector<std::vector<uint8_t>> compressedBuffers( sourceFiles.size() );
    for ( size_t i = 0; i < sourceFiles.size(); ++i )
    {
        int bound = lzav::lzav_compress_bound( ( int ) sourceFiles[i].size() );
        compressedBuffers[i].resize( bound );
    }

    // 3. Measure Compression (Memory-to-Memory)
    auto startComp = std::chrono::high_resolution_clock::now();
    for ( size_t i = 0; i < sourceFiles.size(); ++i )
    {
        int compLen = lzav::lzav_compress_default(
            sourceFiles[i].data(),
            compressedBuffers[i].data(),
            ( int ) sourceFiles[i].size(),
            ( int ) compressedBuffers[i].size()
        );
        ASSERT_GT( compLen, 0 );
        compressedBuffers[i].resize( compLen ); // Shrink to actual compressed size
    }
    auto endComp = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> compTimeSec = endComp - startComp;
    printf( "Compression:   %.2f ms (%.3f GB/sec)\n", compTimeSec.count() * 1000.0, totalGB / compTimeSec.count() );

    // 4. Pre-allocate decompression buffers (exclude alloc from timing)
    std::vector<std::vector<uint8_t>> decompressedBuffers( sourceFiles.size() );
    for ( size_t i = 0; i < sourceFiles.size(); ++i )
    {
        decompressedBuffers[i].resize( sourceFiles[i].size() );
    }

    // 5. Measure Decompression (Memory-to-Memory)
    auto startDecomp = std::chrono::high_resolution_clock::now();
    for ( size_t i = 0; i < sourceFiles.size(); ++i )
    {
        int res = lzav::lzav_decompress(
            compressedBuffers[i].data(),
            decompressedBuffers[i].data(),
            ( int ) compressedBuffers[i].size(),
            ( int ) decompressedBuffers[i].size()
        );
        ASSERT_EQ( res, ( int ) sourceFiles[i].size() );
    }
    auto endDecomp = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> decompTimeSec = endDecomp - startDecomp;
    printf( "Decompression: %.2f ms (%.3f GB/sec)\n", decompTimeSec.count() * 1000.0, totalGB / decompTimeSec.count() );
}

UTEST( Vark, PersistentMode )
{
    const std::string archivePath = "persistent_test.vark";
    Vark vark;
    ASSERT_TRUE( VarkCreateArchive( vark, archivePath, VARK_WRITE | VARK_PERSISTENT_FP ) );
    for ( const auto& filePath : TEST_FILES ) {
        ASSERT_TRUE( VarkCompressAppendFile( vark, filePath ) );
    }
    VarkCloseArchive( vark );

    // Load with persistent FP
    Vark varkLoaded;
    ASSERT_TRUE( VarkLoadArchive( varkLoaded, archivePath, VARK_PERSISTENT_FP ) );
    ASSERT_TRUE( varkLoaded.fp != nullptr );

    // Decompress multiple times
    for ( int i = 0; i < 3; ++i )
    {
        for ( const auto& filePath : TEST_FILES )
        {
            std::vector<uint8_t> data;
            ASSERT_TRUE( VarkDecompressFile( varkLoaded, filePath, data ) );
            ASSERT_GT( data.size(), 0 );
        }
    }

    VarkCloseArchive( varkLoaded );
    remove( archivePath.c_str() );
}

UTEST( Vark, PerfPersistent )
{
    const std::string archivePath = "perf_persistent.vark";
    Vark vark;
    ASSERT_TRUE( VarkCreateArchive( vark, archivePath, VARK_WRITE | VARK_PERSISTENT_FP ) );

    uint64_t totalSizeBytes = 0;
    for ( const auto& filePath : TEST_FILES ) {
        ASSERT_TRUE( VarkCompressAppendFile( vark, filePath ) );
        totalSizeBytes += std::filesystem::file_size( filePath );
    }
    VarkCloseArchive( vark );
    double totalGB = ( double ) totalSizeBytes / ( 1000.0 * 1000.0 * 1000.0 );

    Vark varkLoaded;
    ASSERT_TRUE( VarkLoadArchive( varkLoaded, archivePath, VARK_PERSISTENT_FP ) );

    printf( "\n[ Performance Results (Persistent FP + TempBuffer) ]\n" );
    std::vector<uint8_t> data;
    data.reserve( 1024 * 1024 * 16 );
    auto startDecomp = std::chrono::high_resolution_clock::now();

    // Run multiple iterations to amortize overhead and get better measurement
    int iterations = 10;
    for ( int i = 0; i < iterations; ++i )
    {
        for ( const auto& filePath : TEST_FILES ) {
            ASSERT_TRUE( VarkDecompressFile( varkLoaded, filePath, data ) );
        }
    }
    auto endDecomp = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> decompTimeSec = endDecomp - startDecomp;

    double totalProcessedGB = totalGB * iterations;
    printf( "Decompression: %.2f ms (%.3f GB/sec)\n", decompTimeSec.count() * 1000.0, totalProcessedGB / decompTimeSec.count() );

    VarkCloseArchive( varkLoaded );
    remove( archivePath.c_str() );
}

UTEST( Vark, PerfMapped )
{
    const std::string archivePath = "perf_mapped.vark";
    Vark vark;
    ASSERT_TRUE( VarkCreateArchive( vark, archivePath, VARK_WRITE | VARK_PERSISTENT_FP ) );

    uint64_t totalSizeBytes = 0;
    for ( const auto& filePath : TEST_FILES )
    {
        ASSERT_TRUE( VarkCompressAppendFile( vark, filePath ) );
        totalSizeBytes += std::filesystem::file_size( filePath );
    }
    VarkCloseArchive( vark );
    double totalGB = ( double ) totalSizeBytes / ( 1000.0 * 1000.0 * 1000.0 );

    Vark varkLoaded;
    ASSERT_TRUE( VarkLoadArchive( varkLoaded, archivePath, VARK_MMAP ) );

    printf( "\n[ Performance Results (Memory Mapped) ]\n" );
    std::vector<uint8_t> data;
    data.reserve( 1024 * 1024 * 16 );
    auto startDecomp = std::chrono::high_resolution_clock::now();

    int iterations = 10;
    for ( int i = 0; i < iterations; ++i )
    {
        for ( const auto& filePath : TEST_FILES )
        {
            ASSERT_TRUE( VarkDecompressFile( varkLoaded, filePath, data ) );
        }
    }
    auto endDecomp = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> decompTimeSec = endDecomp - startDecomp;

    double totalProcessedGB = totalGB * iterations;
    printf( "Decompression: %.2f ms (%.3f GB/sec)\n", decompTimeSec.count() * 1000.0, totalProcessedGB / decompTimeSec.count() );

    VarkCloseArchive( varkLoaded );
    remove( archivePath.c_str() );
}

UTEST( Vark, NegativeTests )
{
    Vark vark;
    const std::string path = "negative_test.vark";

    // Test Create with both flags
    ASSERT_FALSE( VarkCreateArchive( vark, path, VARK_WRITE | VARK_MMAP ) );

    // Test Load with both flags
    // First create a valid archive so load can even try to open it
    ASSERT_TRUE( VarkCreateArchive( vark, path, VARK_WRITE ) );
    VarkCloseArchive( vark );
    ASSERT_FALSE( VarkLoadArchive( vark, path, VARK_WRITE | VARK_MMAP ) );

    remove( path.c_str() );
}

UTEST( CLI, Help )
{
    char* argv[] = { ( char* ) "vark" };
    ASSERT_EQ( 1, vark_test_main( 1, argv ) );
}

UTEST( CLI, CreateListVerify )
{
    const char* archive = "cli_test.vark";
    const char* file = "tests/alice_in_wonderland.txt";

    // Create
    {
        char* argv[] = { ( char* ) "vark", ( char* ) "-c", ( char* ) archive, ( char* ) file };
        ASSERT_EQ( 0, vark_test_main( 4, argv ) );
    }

    // List
    {
        char* argv[] = { ( char* ) "vark", ( char* ) "-l", ( char* ) archive };
        ASSERT_EQ( 0, vark_test_main( 3, argv ) );
    }

    // Verify
    {
        char* argv[] = { ( char* ) "vark", ( char* ) "-v", ( char* ) archive };
        ASSERT_EQ( 0, vark_test_main( 3, argv ) );
    }

    // Smart Create/Append (if not exists -> create)
    std::string archive2 = "cli_smart.vark";
    {
        char* argv[] = { ( char* ) "vark", ( char* ) archive2.c_str(), ( char* ) file };
        ASSERT_EQ( 0, vark_test_main( 3, argv ) );
    }
    // Smart Append
    {
        char* argv[] = { ( char* ) "vark", ( char* ) archive2.c_str(), ( char* ) "tests/swoosh_1.wav" };
        ASSERT_EQ( 0, vark_test_main( 3, argv ) );
    }
    // Smart Extract (no args)
    {
        char* argv[] = { ( char* ) "vark", ( char* ) archive2.c_str() };
        ASSERT_EQ( 0, vark_test_main( 2, argv ) );
    }

    std::filesystem::remove( archive );
    std::filesystem::remove( archive2 );
}

UTEST( CLI, DirectoryRecursion )
{
    const char* archive = "cli_recursive.vark";
#ifdef _WIN32
    const char* dir = "tests\\testa";
#else
    const char* dir = "tests/testa";
#endif

    // Create archive from directory
    {
        char* argv[] = { ( char* ) "vark", ( char* ) "-c", ( char* ) archive, ( char* ) dir };
        ASSERT_EQ( 0, vark_test_main( 4, argv ) );
    }

    // Verify contents
    Vark vark;
    ASSERT_TRUE( VarkLoadArchive( vark, archive ) );
    
    bool foundA = false;
    bool foundB = false;
    bool foundC = false;

    // Vark ALWAYS stores paths with forward slashes (generic format)
    // regardless of the platform's preferred separator.
    std::string pathA = "tests/testa/alice_in_wonderland.txt";
    std::string pathB = "tests/testa/testb/alice_in_wonderland.txt";
    std::string pathC = "tests/testa/testc/alice_in_wonderland.txt";
    
    for ( const auto& f : vark.files )
    {
        std::string s = f.path.string();
        if ( s == pathA ) foundA = true;
        if ( s == pathB ) foundB = true;
        if ( s == pathC ) foundC = true;
    }

    ASSERT_TRUE( foundA );
    ASSERT_TRUE( foundB );
    ASSERT_TRUE( foundC );

    VarkCloseArchive( vark );
    std::filesystem::remove( archive );
}

UTEST( Vark, ShardedCompression )
{
    const std::string archivePath = "sharded_test.vark";
    const std::string largeFile = "tests/Apophysis-250901-101.png"; // Use a larger file to ensure multiple shards

    // 1. Create Sharded Archive
    Vark vark;
    ASSERT_TRUE( VarkCreateArchive( vark, archivePath, VARK_WRITE ) );
    ASSERT_TRUE( VarkCompressAppendFile( vark, largeFile, VARK_COMPRESS_SHARDED ) );
    VarkCloseArchive( vark );

    // 2. Load and Verify Shard Metadata
    Vark varkLoaded;
    ASSERT_TRUE( VarkLoadArchive( varkLoaded, archivePath ) );
    ASSERT_EQ( (size_t)1, varkLoaded.files.size() );
    ASSERT_EQ( (uint32_t)VARK_DEFAULT_SHARD_SIZE, varkLoaded.files[0].shardSize );

    // 3. Decompress and Verify Data
    std::vector<uint8_t> originalData = ReadFileContent( largeFile );
    std::vector<uint8_t> decompressedData;
    
    // Test with both Standard I/O and MMAP if possible
    ASSERT_TRUE( VarkDecompressFile( varkLoaded, largeFile, decompressedData ) );
    ASSERT_EQ( originalData.size(), decompressedData.size() );
    ASSERT_EQ( SimpleHash( originalData ), SimpleHash( decompressedData ) );

    VarkCloseArchive( varkLoaded );
    
    // 4. Test MMAP Decompression
    Vark varkMmap;
    ASSERT_TRUE( VarkLoadArchive( varkMmap, archivePath, VARK_MMAP ) );
    std::vector<uint8_t> decompressedDataMmap;
    ASSERT_TRUE( VarkDecompressFile( varkMmap, largeFile, decompressedDataMmap ) );
    ASSERT_EQ( originalData.size(), decompressedDataMmap.size() );
    ASSERT_EQ( SimpleHash( originalData ), SimpleHash( decompressedDataMmap ) );
    VarkCloseArchive( varkMmap );

    remove( archivePath.c_str() );
}

UTEST( Vark, PartialShardedDecompression )
{
    const std::string archivePath = "sharded_partial.vark";
    const std::string largeFile = "tests/Apophysis-250901-101.png";
    std::vector<uint8_t> originalData = ReadFileContent( largeFile );
    ASSERT_GT( originalData.size(), ( size_t ) ( 512 * 1024 ) ); // Ensure it's big enough

    // 1. Create Sharded Archive
    Vark vark;
    ASSERT_TRUE( VarkCreateArchive( vark, archivePath, VARK_WRITE ) );
    ASSERT_TRUE( VarkCompressAppendFile( vark, largeFile, VARK_COMPRESS_SHARDED ) );
    VarkCloseArchive( vark );

    // 2. Test Partial Reads (Various offsets and sizes)
    struct TestCase { uint64_t off; uint64_t size; };
    std::vector<TestCase> cases = {
        { 0, 100 },                     // Start of file
        { 12345, 5000 },                // Middle of first shard
        { 128 * 1024 - 10, 20 },        // Span across shard boundary
        { 256 * 1024 + 50, 1000 },      // Entirely within second/third shard
        { originalData.size() - 100, 100 } // End of file
    };

    Vark varkLoaded;
    ASSERT_TRUE( VarkLoadArchive( varkLoaded, archivePath ) );

    for ( const auto& c : cases )
    {
        std::vector<uint8_t> partial;
        ASSERT_TRUE( VarkDecompressFileSharded( varkLoaded, largeFile, c.off, c.size, partial ) );
        ASSERT_EQ( ( size_t ) c.size, partial.size() );
        ASSERT_EQ( 0, memcmp( partial.data(), originalData.data() + c.off, ( size_t ) c.size ) );
    }

    // 3. Test with MMAP
    Vark varkMmap;
    ASSERT_TRUE( VarkLoadArchive( varkMmap, archivePath, VARK_MMAP ) );
    for ( const auto& c : cases )
    {
        std::vector<uint8_t> partial;
        ASSERT_TRUE( VarkDecompressFileSharded( varkMmap, largeFile, c.off, c.size, partial ) );
        ASSERT_EQ( ( size_t ) c.size, partial.size() );
        ASSERT_EQ( 0, memcmp( partial.data(), originalData.data() + c.off, ( size_t ) c.size ) );
    }

    VarkCloseArchive( varkLoaded );
    VarkCloseArchive( varkMmap );
    remove( archivePath.c_str() );
}

UTEST( Vark, LegacyCompatibility )
{
    const std::string archivePath = "tests/compat_v102.vark";
    
    // Ensure the legacy file exists before testing
    if ( !std::filesystem::exists( archivePath ) ) 
    {
        printf( "[WARNING] Legacy test file not found, skipping LegacyCompatibility test.\n" );
        return;
    }

    Vark vark;
    ASSERT_TRUE( VarkLoadArchive( vark, archivePath ) );
    ASSERT_GT( vark.files.size(), (size_t)0 );

    // Verify all files have shardSize == 0 (Default/Legacy)
    for ( const auto& f : vark.files )
    {
        ASSERT_EQ( (uint32_t)0, f.shardSize );
    }

    // Verify we can decompress (just check the first file)
    if ( !vark.files.empty() )
    {
        std::vector<uint8_t> data;
        // We don't have the original hash to compare against, but we can check if decompress returns true
        ASSERT_TRUE( VarkDecompressFile( vark, vark.files[0].path.string(), data ) );
        ASSERT_GT( data.size(), (size_t)0 );
    }

    VarkCloseArchive( vark );
    // remove( archivePath.c_str() ); // DO NOT remove legacy test file
}

UTEST( CLI, LegacyCompatibility )
{
    const char* archive = "tests/compat_v102.vark";
    if ( !std::filesystem::exists( archive ) ) return;

    // 1. List
    {
        char* argv[] = { ( char* ) "vark", ( char* ) "-l", ( char* ) archive };
        ASSERT_EQ( 0, vark_test_main( 3, argv ) );
    }

    // 2. Verify
    {
        char* argv[] = { ( char* ) "vark", ( char* ) "-v", ( char* ) archive };
        ASSERT_EQ( 0, vark_test_main( 3, argv ) );
    }

    // 3. Extract (Standard Mode)
    {
        char* argv[] = { ( char* ) "vark", ( char* ) "-x", ( char* ) archive };
        ASSERT_EQ( 0, vark_test_main( 3, argv ) );
    }

    // Cleanup extracted files
    std::filesystem::remove_all( "testa" );
    std::filesystem::remove( "alice_in_wonderland.txt" );
}

UTEST( Vark, ShardedEdgeCases )
{
    const std::string archivePath = "sharded_edge.vark";
    const std::string emptyFile = "empty.dat";
    const std::string smallFile = "small.dat";
    const std::string exactFile = "exact.dat";  // 128KB
    const std::string boundaryFile = "boundary.dat"; // 128KB + 1

    // Generate test files
    {
        FILE* fp = fopen( emptyFile.c_str(), "wb" ); fclose( fp );
        
        fp = fopen( smallFile.c_str(), "wb" );
        fprintf( fp, "Small text file" );
        fclose( fp );

        std::vector<uint8_t> exactData( VARK_DEFAULT_SHARD_SIZE, 'x' );
        fp = fopen( exactFile.c_str(), "wb" );
        fwrite( exactData.data(), 1, exactData.size(), fp );
        fclose( fp );

        std::vector<uint8_t> boundaryData( VARK_DEFAULT_SHARD_SIZE + 1, 'y' );
        fp = fopen( boundaryFile.c_str(), "wb" );
        fwrite( boundaryData.data(), 1, boundaryData.size(), fp );
        fclose( fp );
    }

    Vark vark;
    ASSERT_TRUE( VarkCreateArchive( vark, archivePath, VARK_WRITE ) );
    ASSERT_TRUE( VarkCompressAppendFile( vark, emptyFile, VARK_COMPRESS_SHARDED ) );
    ASSERT_TRUE( VarkCompressAppendFile( vark, smallFile, VARK_COMPRESS_SHARDED ) );
    ASSERT_TRUE( VarkCompressAppendFile( vark, exactFile, VARK_COMPRESS_SHARDED ) );
    ASSERT_TRUE( VarkCompressAppendFile( vark, boundaryFile, VARK_COMPRESS_SHARDED ) );
    VarkCloseArchive( vark );

    Vark varkLoaded;
    ASSERT_TRUE( VarkLoadArchive( varkLoaded, archivePath ) );

    // Verify Empty
    {
        std::vector<uint8_t> data;
        ASSERT_TRUE( VarkDecompressFile( varkLoaded, emptyFile, data ) );
        ASSERT_EQ( (size_t)0, data.size() );
        ASSERT_TRUE( VarkDecompressFileSharded( varkLoaded, emptyFile, 0, 0, data ) );
    }

    // Verify Small
    {
        std::vector<uint8_t> data;
        ASSERT_TRUE( VarkDecompressFile( varkLoaded, smallFile, data ) );
        std::string s( data.begin(), data.end() );
        ASSERT_EQ( std::string("Small text file"), s );
        
        // Partial on small
        ASSERT_TRUE( VarkDecompressFileSharded( varkLoaded, smallFile, 0, 5, data ) );
        s = std::string( data.begin(), data.end() );
        ASSERT_EQ( std::string("Small"), s );
    }

    // Verify Exact (1 shard boundary)
    {
        std::vector<uint8_t> data;
        ASSERT_TRUE( VarkDecompressFile( varkLoaded, exactFile, data ) );
        ASSERT_EQ( (size_t)VARK_DEFAULT_SHARD_SIZE, data.size() );
    }

    // Verify Boundary (2 shards, second is 1 byte)
    {
        std::vector<uint8_t> data;
        ASSERT_TRUE( VarkDecompressFile( varkLoaded, boundaryFile, data ) );
        ASSERT_EQ( (size_t)VARK_DEFAULT_SHARD_SIZE + 1, data.size() );
        
        // Read crossing boundary
        ASSERT_TRUE( VarkDecompressFileSharded( varkLoaded, boundaryFile, VARK_DEFAULT_SHARD_SIZE - 10, 11, data ) );
        ASSERT_EQ( (size_t)11, data.size() );
        for( int i=0; i<11; ++i ) ASSERT_EQ( 'y', data[i] );
    }

    VarkCloseArchive( varkLoaded );
    std::filesystem::remove( archivePath );
    std::filesystem::remove( emptyFile );
    std::filesystem::remove( smallFile );
    std::filesystem::remove( exactFile );
    std::filesystem::remove( boundaryFile );
}

UTEST( Vark, ShardedFuzz )
{
    const std::string archivePath = "sharded_fuzz.vark";
    const std::string fuzzFile = "fuzz.dat";
    const size_t fileSize = 5 * 1024 * 1024; // 5MB

    // Generate random data
    std::vector<uint8_t> originalData( fileSize );
    for( size_t i=0; i<fileSize; ++i ) originalData[i] = (uint8_t)(i & 0xFF);

    FILE* fp = fopen( fuzzFile.c_str(), "wb" );
    fwrite( originalData.data(), 1, fileSize, fp );
    fclose( fp );

    Vark vark;
    ASSERT_TRUE( VarkCreateArchive( vark, archivePath, VARK_WRITE ) );
    ASSERT_TRUE( VarkCompressAppendFile( vark, fuzzFile, VARK_COMPRESS_SHARDED ) );
    VarkCloseArchive( vark );

    Vark varkLoaded;
    ASSERT_TRUE( VarkLoadArchive( varkLoaded, archivePath ) );

    // 100 Random reads
    srand(123);
    for( int i=0; i<100; ++i )
    {
        uint64_t off = rand() % ( fileSize - 1 );
        uint64_t maxLen = fileSize - off;
        uint64_t len = ( rand() % 100000 ) + 1; // up to ~100KB
        if ( len > maxLen ) len = maxLen;

        std::vector<uint8_t> part;
        ASSERT_TRUE( VarkDecompressFileSharded( varkLoaded, fuzzFile, off, len, part ) );
        ASSERT_EQ( (size_t)len, part.size() );
        if ( memcmp( part.data(), originalData.data() + off, (size_t)len ) != 0 )
        {
            printf("Mismatch at offset %llu len %llu\n", off, len);
            ASSERT_TRUE( false );
        }
    }

    // Invalid Access Tests
    std::vector<uint8_t> junk;
    ASSERT_FALSE( VarkDecompressFileSharded( varkLoaded, fuzzFile, fileSize, 1, junk ) ); // Start at end
    ASSERT_FALSE( VarkDecompressFileSharded( varkLoaded, fuzzFile, fileSize - 10, 20, junk ) ); // Span past end
    ASSERT_FALSE( VarkDecompressFileSharded( varkLoaded, "nonexistent", 0, 10, junk ) );

    VarkCloseArchive( varkLoaded );
    std::filesystem::remove( archivePath );
    std::filesystem::remove( fuzzFile );
}

UTEST( Vark, ShardedApiOnNonSharded )
{
    const std::string archivePath = "mixed_api.vark";
    const std::string normalFile = "normal.dat";
    
    FILE* fp = fopen( normalFile.c_str(), "wb" );
    fprintf( fp, "Normal compression" );
    fclose( fp );

    Vark vark;
    ASSERT_TRUE( VarkCreateArchive( vark, archivePath, VARK_WRITE ) );
    ASSERT_TRUE( VarkCompressAppendFile( vark, normalFile, 0 ) ); // 0 = standard
    VarkCloseArchive( vark );

    Vark varkLoaded;
    ASSERT_TRUE( VarkLoadArchive( varkLoaded, archivePath ) );

    std::vector<uint8_t> data;
    // Should fail because it's not sharded
    ASSERT_FALSE( VarkDecompressFileSharded( varkLoaded, normalFile, 0, 5, data ) );
    
    // Standard decompression should still work
    ASSERT_TRUE( VarkDecompressFile( varkLoaded, normalFile, data ) );
    
    VarkCloseArchive( varkLoaded );
    std::filesystem::remove( archivePath );
    std::filesystem::remove( normalFile );
}

UTEST_MAIN()