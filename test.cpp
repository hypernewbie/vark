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
    ASSERT_TRUE( VarkLoadArchive( varkLoaded, archivePath, VARK_PERSISTENT_FP | VARK_PERSISTENT_TEMPBUFFER ) );

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
    ASSERT_TRUE( VarkLoadArchive( varkLoaded, archivePath, VARK_MMAP | VARK_PERSISTENT_TEMPBUFFER ) );

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

UTEST_MAIN()