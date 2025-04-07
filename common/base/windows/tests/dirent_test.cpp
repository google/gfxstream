#include <gtest/gtest.h>

#include "dirent.h"

#include <codecvt>
#include <filesystem>
#include <fstream>
#include <locale>
#include <random>

namespace fs = std::filesystem;

// Helper function to create a directory with a specific name
void createDirectory(const fs::path& dirName) { fs::create_directories(dirName); }

// Helper function to create a file with a specific name
void createFile(const fs::path& filename) {
    std::ofstream file(filename);
    file.close();
}

// Helper function to convert UTF-8 to wide string
std::wstring utf8ToWide(const std::string& utf8Str) {
    std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
    return converter.from_bytes(utf8Str);
}

// Helper function to convert wide string to UTF-8
std::string wideToUtf8(const std::wstring& wideStr) {
    std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
    return converter.to_bytes(wideStr);
}

class DirentTest : public ::testing::Test {
   protected:
    // Setup - create a temporary directory for testing
    void SetUp() override {
        // Generate a random directory name
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dist(0, 15);  // For hex characters

        std::string randomDirName = "dirent_test_";
        for (int i = 0; i < 8; ++i) {
            randomDirName += "0123456789abcdef"[dist(gen)];
        }

        tempDir = fs::temp_directory_path() / randomDirName;
        fs::create_directories(tempDir);
    }

    // Teardown - remove the temporary directory
    void TearDown() override {
        try {
            fs::remove_all(tempDir);
        } catch (const fs::filesystem_error& e) {
            std::cerr << "Warning failed to remove directory: " << e.what() << std::endl;
        }
    }

    fs::path tempDir;
};

// Test opendir with an invalid directory name
TEST_F(DirentTest, OpenDirInvalid) {
    DIR* dir = opendir("invalid_dir");
    ASSERT_EQ(nullptr, dir);
    ASSERT_EQ(ENOENT, errno);
}

// Test opendir with a valid directory name
TEST_F(DirentTest, OpenDirValid) {
    DIR* dir = opendir(tempDir.string().c_str());
    ASSERT_NE(nullptr, dir);
    closedir(dir);
}

// Test readdir with an empty directory
TEST_F(DirentTest, ReadDirEmpty) {
    DIR* dir = opendir(tempDir.string().c_str());
    ASSERT_NE(nullptr, dir);
    struct dirent* entry = readdir(dir);
    ASSERT_EQ(nullptr, entry);
    closedir(dir);
}

// Test readdir with some files
TEST_F(DirentTest, ReadDirBasic) {
    createFile(tempDir / "file1.txt");
    createFile(tempDir / "file2.txt");

    DIR* dir = opendir(tempDir.string().c_str());
    ASSERT_NE(nullptr, dir);

    struct dirent* entry;
    int count = 0;
    while ((entry = readdir(dir)) != nullptr) {
        ASSERT_TRUE(strcmp(entry->d_name, "file1.txt") == 0 ||
                    strcmp(entry->d_name, "file2.txt") == 0);
        count++;
    }
    ASSERT_EQ(2, count);

    closedir(dir);
}

// Test readdir with UTF-8 filenames
TEST_F(DirentTest, ReadDirUtf8) {
    std::wstring filename = L"hiফাইলhi.txt";

    // We expect a utf8 filename..
    std::string filenameu8 = wideToUtf8(filename); // u8"hiফাইলhi.txt";
    createFile(tempDir / filename);

    ASSERT_TRUE(fs::exists(tempDir / filename));
    DIR* dir = opendir(tempDir.string().c_str());
    ASSERT_NE(nullptr, dir);

    struct dirent* entry = readdir(dir);
    ASSERT_NE(nullptr, entry);
    ASSERT_EQ(filenameu8, entry->d_name);

    closedir(dir);
}

// Test rewinddir
TEST_F(DirentTest, RewindDir) {
    createFile(tempDir / "file1.txt");
    createFile(tempDir / "file2.txt");

    DIR* dir = opendir(tempDir.string().c_str());
    ASSERT_NE(nullptr, dir);

    // Read the first entry
    struct dirent* entry1 = readdir(dir);
    ASSERT_NE(nullptr, entry1);

    // Rewind the directory
    rewinddir(dir);

    // Read the first entry again
    struct dirent* entry2 = readdir(dir);
    ASSERT_NE(nullptr, entry2);
    ASSERT_STREQ(entry1->d_name, entry2->d_name);

    closedir(dir);
}

// Test telldir/seekdir (limited functionality)
TEST_F(DirentTest, TellSeekDir) {
    createFile(tempDir / "file1.txt");
    createFile(tempDir / "file2.txt");
    createFile(tempDir / "file3.txt");

    DIR* dir = opendir(tempDir.string().c_str());
    ASSERT_NE(nullptr, dir);

    // Get initial position (should be 0)
    long initialPos = telldir(dir);
    ASSERT_EQ(0, initialPos);

    // Read the first entry
    struct dirent* entry1 = readdir(dir);
    ASSERT_NE(nullptr, entry1);

    // Get position (should be 1 now)
    long pos1 = telldir(dir);
    ASSERT_EQ(1, pos1);

    // Read the second entry
    struct dirent* entry2 = readdir(dir);
    ASSERT_NE(nullptr, entry2);

    // Get position (should be 2 now)
    long pos2 = telldir(dir);
    ASSERT_EQ(2, pos2);

    // Seek to beginning
    seekdir(dir, 0);
    long currentPos = telldir(dir);
    ASSERT_EQ(0, currentPos);

    // Verify we can read again from the beginning
    struct dirent* entry3 = readdir(dir);
    ASSERT_NE(nullptr, entry3);
    ASSERT_STREQ(entry1->d_name, entry3->d_name);

    // Seek to position 1
    seekdir(dir, 1);
    currentPos = telldir(dir);
    ASSERT_EQ(1, currentPos);

    // Verify we can read the second entry again
    struct dirent* entry4 = readdir(dir);
    ASSERT_NE(nullptr, entry4);
    ASSERT_STREQ(entry2->d_name, entry4->d_name);

    // Seek to end
    seekdir(dir, -1);
    currentPos = telldir(dir);
    ASSERT_EQ(-1, currentPos);

    // Check that readdir returns nullptr after seekdir(-1)
    struct dirent* entry5 = readdir(dir);
    ASSERT_EQ(nullptr, entry5);

    // Seek to position 2
    seekdir(dir, 2);
    currentPos = telldir(dir);
    ASSERT_EQ(2, currentPos);

    // Read the third entry
    struct dirent* entry6 = readdir(dir);
    ASSERT_NE(nullptr, entry6);
    ASSERT_STREQ("file3.txt", entry6->d_name);

    // Try seeking beyond the end
    seekdir(dir, 10);
    currentPos = telldir(dir);
    ASSERT_EQ(errno, EINVAL); // Bad!

    // Verify that readdir returns nullptr
    struct dirent* entry7 = readdir(dir);
    ASSERT_EQ(nullptr, entry7);

    closedir(dir);
}

// Test closedir
TEST_F(DirentTest, CloseDir) {
    DIR* dir = opendir(tempDir.string().c_str());
    ASSERT_NE(nullptr, dir);
    int result = closedir(dir);
    ASSERT_EQ(0, result);
}

// Test extended path
TEST_F(DirentTest, ExtendedPath) {
    // Create a path that exceeds MAX_PATH
    std::wstring longDirName = L"\\\\?\\" + tempDir.wstring() + L"\\long_directory_name";
    for (int i = 0; i < 30; ++i) {
        longDirName += L"\\subdir";
    }

    // Create the long directory structure
    ASSERT_TRUE(fs::create_directories(longDirName));

    // Create a file within the long directory
    std::wstring longFileName = longDirName + L"\\file.txt";
    std::ofstream file(longFileName);
    ASSERT_TRUE(file.is_open());
    file.close();

    // Convert to UTF-8 for opendir
    std::string longDirNameUtf8 = wideToUtf8(longDirName);

    // Open the directory using opendir
    DIR* dir = opendir(longDirNameUtf8.c_str());
    ASSERT_NE(nullptr, dir);

    // Read directory entries
    struct dirent* entry;
    bool found = false;
    while ((entry = readdir(dir)) != nullptr) {
        if (strcmp(entry->d_name, "file.txt") == 0) {
            found = true;
            break;
        }
    }

    // Check if the file was found
    ASSERT_TRUE(found);

    // Close the directory
    closedir(dir);

    // Cleanup
    fs::remove(longFileName);
    ASSERT_FALSE(fs::exists(longFileName));
}

// Test various error conditions
TEST_F(DirentTest, ErrorConditions) {
    // Invalid directory name
    DIR* dir = opendir(nullptr);
    ASSERT_EQ(nullptr, dir);
    ASSERT_EQ(EINVAL, errno);

    // Directory not found
    dir = opendir("nonexistent_directory");
    ASSERT_EQ(nullptr, dir);
    ASSERT_EQ(ENOENT, errno);

    // Not a directory
    createFile(tempDir / "file.txt");
    dir = opendir((tempDir / "file.txt").c_str());
    ASSERT_EQ(nullptr, dir);
    ASSERT_EQ(ENOTDIR, errno);

    // Invalid DIR pointer
    struct dirent* entry = readdir(nullptr);
    ASSERT_EQ(nullptr, entry);
    ASSERT_EQ(EBADF, errno);

    int result = closedir(nullptr);
    ASSERT_EQ(-1, result);
    ASSERT_EQ(EBADF, errno);

    rewinddir(nullptr);
    ASSERT_EQ(EBADF, errno);

    seekdir(nullptr, 0);
    ASSERT_EQ(EBADF, errno);

    long pos = telldir(nullptr);
    ASSERT_EQ(-1, pos);
    ASSERT_EQ(EBADF, errno);
}