#include <iostream>
#include <string>
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <vector>
#include <filesystem>
#include <fstream>
#include <windows.h>
#include <cstdlib> // for _dupenv_s
#include <cerrno>  // for errno_t



// Callback function to handle the response from libcurl
size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

// Function to fetch data from the Pastebin URL
std::string fetchDataFromPastebin(const std::string& url) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        std::cerr << "Curl initialization failed!" << std::endl;
        throw std::runtime_error("Curl initialization failed.");
    }

    std::string readBuffer;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        std::cerr << "Curl request failed: " << curl_easy_strerror(res) << std::endl;
        curl_easy_cleanup(curl);
        throw std::runtime_error("Curl request failed.");
    }

    curl_easy_cleanup(curl);
    return readBuffer;
}

// Function to resolve environment variables in paths
// Function to resolve environment variables in paths
std::string resolveEnvironmentVariables(const std::string& path) {
    std::string resolvedPath = path;

    // Check if the path contains the environment variable
    if (resolvedPath.find("%LOCALAPPDATA%") != std::string::npos) {
        // Get the environment variable for LOCALAPPDATA
        char* localAppData = nullptr;
        size_t len = 0;
        errno_t err = _dupenv_s(&localAppData, &len, "LOCALAPPDATA");

        if (err == 0 && localAppData != nullptr) {
            // Replace "%LOCALAPPDATA%" in the string with the actual path
            // Add a backslash to ensure correct path structure
            std::string replacementPath = std::string(localAppData) + "\\";
            resolvedPath.replace(resolvedPath.find("%LOCALAPPDATA%"), 15, replacementPath);

            free(localAppData); // Free the allocated memory
        }
        else {
            std::cerr << "Failed to get LOCALAPPDATA environment variable." << std::endl;
        }
    }

    return resolvedPath;
}

// Function to get available drives
std::vector<std::string> getAvailableDrives() {
    std::vector<std::string> drives;
    DWORD drivesBitmask = GetLogicalDrives();

    for (char letter = 'A'; letter <= 'Z'; ++letter) {
        if (drivesBitmask & (1 << (letter - 'A'))) {
            drives.push_back(std::string(1, letter) + ":");
        }
    }
    return drives;
}

// Function to validate folders and log errors
void validateFolders(const std::vector<std::string>& folders, std::vector<std::string>& Fmeory) {
    std::vector<std::string> availableDrives = getAvailableDrives();
    std::ofstream errorLog("errors.txt", std::ios::app); // Open error log for appending

    for (const auto& folder : folders) {
        bool folderFound = false;
        std::string resolvedFolder = resolveEnvironmentVariables(folder);

        for (const auto& drive : availableDrives) {
            std::string fullPath = resolvedFolder;
            size_t pos;
            while ((pos = fullPath.find("{DRIVE}")) != std::string::npos) {
                fullPath.replace(pos, 7, drive);
            }

            if (std::filesystem::exists(fullPath)) {
                Fmeory.push_back(fullPath); // Add found path to Fmeory
                folderFound = true;
                break;
            }
        }

        if (!folderFound) {
            errorLog << "Folder not found: " << folder << std::endl;
        }
    }

    errorLog.close();
}






void validateFiles(const std::vector<std::string>& filePaths, std::vector<std::string>& foundFiles) {
    std::ofstream errorLog("errors.txt", std::ios::app);

    // Additional drives to search
    std::vector<std::string> searchDrives = { "C:\\", "D:\\", "E:\\", "F:\\" };

    for (const auto& filePath : filePaths) {
        bool fileFound = false;
        std::string resolvedFilePath = resolveEnvironmentVariables(filePath);

        // Ensure the resolved path has a backslash after %LOCALAPPDATA%
        if (resolvedFilePath.find("%LOCALAPPDATA%") != std::string::npos) {
            size_t pos = resolvedFilePath.find("%LOCALAPPDATA%");
            if (resolvedFilePath[pos + 15] != '\\') {
                resolvedFilePath.insert(pos + 15, "\\");
            }
        }

        // Prepare potential search paths
        std::vector<std::string> pathsToCheck;

        // If no drive is specified, search multiple drives
        if (resolvedFilePath.find(":\\") == std::string::npos) {
            for (const auto& drive : searchDrives) {
                pathsToCheck.push_back(drive + resolvedFilePath);
            }
        }
        else {
            // Use resolved path as is if it already includes a drive letter
            pathsToCheck.push_back(resolvedFilePath);
        }

        // Search through potential paths
        for (const auto& fullPath : pathsToCheck) {
            // Normalize path separators
            std::string normalizedPath = fullPath;
            std::replace(normalizedPath.begin(), normalizedPath.end(), '/', '\\');

            // Log the path being checked
            errorLog << "Checking path: " << normalizedPath << std::endl;

            try {
                // Check file existence with detailed error handling
                if (std::filesystem::exists(normalizedPath)) {
                    // Additional checks
                    if (std::filesystem::is_regular_file(normalizedPath)) {
                        foundFiles.push_back(normalizedPath);
                        fileFound = true;

                        // Log successful file find
                        errorLog << "File found: " << normalizedPath << std::endl;
                        break; // Stop searching once file is found
                    }
                    else {
                        errorLog << "Path exists but is not a regular file: " << normalizedPath << std::endl;
                    }
                }
            }
            catch (const std::filesystem::filesystem_error& e) {
                // Catch and log any filesystem-related errors
                errorLog << "Filesystem error for path " << normalizedPath << ": " << e.what() << std::endl;
            }
        }

        // Log if file was not found after all searches
        if (!fileFound) {
            errorLog << "File not found: " << resolvedFilePath << std::endl;
        }
    }

    errorLog.close();
}

// Function to delete registry keys stored in the rkey vector
void deleteRegistryKeys(const std::vector<std::string>& rkey) {
    for (const auto& key : rkey) {
        // Split the registry path into hive and subkey
        size_t pos = key.find('\\');
        if (pos == std::string::npos) {
            std::cerr << "Invalid registry key format: " << key << std::endl;
            continue;
        }

        std::string hive = key.substr(0, pos);
        std::string subKey = key.substr(pos + 1);

        // Map hive string to predefined registry handles
        HKEY hKey;
        if (hive == "HKLM") {
            hKey = HKEY_LOCAL_MACHINE;
        }
        else if (hive == "HKCU") {
            hKey = HKEY_CURRENT_USER;
        }
        else if (hive == "HKCR") {
            hKey = HKEY_CLASSES_ROOT;
        }
        else if (hive == "HKU") {
            hKey = HKEY_USERS;
        }
        else if (hive == "HKCC") {
            hKey = HKEY_CURRENT_CONFIG;
        }
        else {
            std::cerr << "Unsupported registry hive: " << hive << std::endl;
            continue;
        }

        // Attempt to delete the registry key
        LSTATUS status = RegDeleteKeyExA(hKey, subKey.c_str(), KEY_WOW64_64KEY, 0);
        if (status == ERROR_SUCCESS) {
            std::cout << "Successfully deleted registry key: " << key << std::endl;
        }
        else {
            std::cerr << "Failed to delete registry key: " << key
                << " (Error code: " << status << ")" << std::endl;
        }
    }
}


// Function to delete files and handle exceptions gracefully
void deleteFiles(const std::vector<std::string>& files) {
    std::ofstream errorLog("errors.txt", std::ios::app);
    for (const auto& file : files) {
        try {
            if (std::filesystem::exists(file)) {
                std::filesystem::remove(file);
                std::cout << "Deleted file: " << file << std::endl;
            }
        }
        catch (const std::exception& e) {
            errorLog << "Failed to delete file: " << file << " (" << e.what() << ")" << std::endl;
        }
    }
    errorLog.close();
}

// Function to delete folders and handle exceptions gracefully
void deleteFolders(const std::vector<std::string>& folders) {
    std::ofstream errorLog("errors.txt", std::ios::app);
    for (const auto& folder : folders) {
        try {
            if (std::filesystem::exists(folder)) {
                std::filesystem::remove_all(folder);
                std::cout << "Deleted folder: " << folder << std::endl;
            }
        }
        catch (const std::exception& e) {
            errorLog << "Failed to delete folder: " << folder << " (" << e.what() << ")" << std::endl;
        }
    }
    errorLog.close();
}


// Function to delete temporary files
void deleteTempFiles() {
    char tempPath[MAX_PATH];
    std::ofstream errorLog("errors.txt", std::ios::app); // Open error log

    if (GetTempPathA(MAX_PATH, tempPath) > 0) {
        try {
            // Iterate over the files in the temp directory
            for (const auto& entry : std::filesystem::directory_iterator(tempPath)) {
                try {
                    // Attempt to delete each file
                    std::filesystem::remove(entry.path());
                    std::cout << "Deleted temporary file: " << entry.path() << std::endl;
                }
                catch (const std::exception& e) {
                    // Log the error if a file cannot be deleted (likely because it's in use)
                    errorLog << "Failed to delete file: " << entry.path()
                        << " (" << e.what() << ")" << std::endl;
                    std::cerr << "Failed to delete file: " << entry.path()
                        << " (" << e.what() << ")" << std::endl;
                }
            }
        }
        catch (const std::exception& e) {
            errorLog << "Failed to delete temporary files from: " << tempPath
                << " (" << e.what() << ")" << std::endl;
            std::cerr << "Failed to delete temporary files: " << e.what() << std::endl;
        }
    }
    else {
        errorLog << "Failed to get temporary path for deletion." << std::endl;
        std::cerr << "Failed to get temporary path." << std::endl;
    }

    errorLog.close(); // Close error log
}


// Function to empty the recycle bin
void emptyRecycleBin() {
    std::ofstream errorLog("errors.txt", std::ios::app); // Open error log

    HRESULT result = SHEmptyRecycleBinA(nullptr, nullptr, SHERB_NOCONFIRMATION | SHERB_NOPROGRESSUI | SHERB_NOSOUND);
    if (SUCCEEDED(result)) {
        std::cout << "Recycle bin emptied." << std::endl;
    }
    else {
        errorLog << "Failed to empty the recycle bin. Error code: " << result << std::endl;
        std::cerr << "Failed to empty the recycle bin. Error code: " << result << std::endl;
    }

    errorLog.close(); // Close error log
}

void enableVirtualTerminalProcessing() {
    DWORD dwMode = 0;
    HANDLE hStdout = GetStdHandle(STD_OUTPUT_HANDLE);

    if (hStdout == INVALID_HANDLE_VALUE) {
        return;
    }

    if (!GetConsoleMode(hStdout, &dwMode)) {
        return;
    }

    dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    SetConsoleMode(hStdout, dwMode);
}
// ANSI escape codes for colors
#define RESET   "\033[0m"
#define MAGENTA "\033[35m"
#define GREEN   "\033[32m"
#define CYAN    "\033[36m"
#define WHITE   "\033[37m"
// Function to print the stylized text with colors
void printBigName() {
    

    // Displaying the "big name" in ASCII Art (the colored part)
    std::cout << MAGENTA << R"(     RUINED EMPIREEEEEEEEEE
    )" << RESET << std::endl;

    // Additional information with colors
    std::cout << GREEN << "Created By ᖉꓵIИED EWbIᖉE | Version Beta For testing purposes | Remote Updates | Join the server GVGyxfB3mF | FOR FORTNITE !\n" << RESET << std::endl;
    std::cout << CYAN << "Do us a favor and submit your error.txt to the discord server so I the developer can take a look at it and continue to update the cleaner\n" << RESET << std::endl;
    std::cout << WHITE << "Side note: consider using divine v1 to change your MAC address, this cleaner doesn't have it implemented yet.\n" << RESET << std::endl;
}


// Function to parse the JSON data and extract specific fields into vectors
void parseJsonData(const std::string& json_data,
    std::vector<std::string>& rkey, 
    std::vector<std::string>& mliles,
    std::vector<std::string>& FLod) {
    try {
        nlohmann::json parsedData = nlohmann::json::parse(json_data);

        // Extract registry keys (if needed)
        for (const auto& key : parsedData["registryKeys"]) {
            rkey.push_back(key.get<std::string>());
        }

        // Extract files and push them to the vector
        for (const auto& file : parsedData["files"]) {
            mliles.push_back(file.get<std::string>());
        }

        // Extract folders if needed (currently not used)
        for (const auto& folder : parsedData["folders"]) {
            FLod.push_back(folder.get<std::string>());
        }
    }
    catch (const std::exception& e) {
        std::cerr << "Error parsing JSON data: " << e.what() << std::endl;
        throw;
    }
}

void setConsoleSize(int width, int height) {
    // Get the current console screen buffer
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(hConsole, &csbi);

    // Set the new screen buffer size (width and height)
    COORD newSize = { static_cast<SHORT>(width), static_cast<SHORT>(height) };
    SetConsoleScreenBufferSize(hConsole, newSize);

    // Set the console window size (the visible part)
    SMALL_RECT windowSize = { 0, 0, static_cast<SHORT>(width - 1), static_cast<SHORT>(height - 1) };
    SetConsoleWindowInfo(hConsole, TRUE, &windowSize);
}

void setConsoleWindowTitle(const std::string& title) {
    // Convert from std::string to std::wstring
    std::wstring wideTitle(title.begin(), title.end());

    // Set the console window title
    SetConsoleTitle(wideTitle.c_str());
}

int main() {
    setConsoleSize(200, 100);
    setConsoleWindowTitle("Empire cleaner Beta / Divine V2 ((BETA))");
    enableVirtualTerminalProcessing();
    const std::string url = "https://pastebin.com/raw/tWDHQZfY"; // Replace with your actual URL
    bool debug = true; // Set to false to suppress debug messages

    try {
        // Fetch raw JSON data (assuming you have a function to fetch this data)
        std::string json_data = fetchDataFromPastebin(url);

        // Declare vectors to hold data
        std::vector<std::string> rkey;   // Registry keys
        std::vector<std::string> mliles; // Files
        std::vector<std::string> FLod;   // Folders

        // Parse JSON data
        parseJsonData(json_data, rkey, mliles, FLod);

        // Create foundFiles and foundFolders vectors
        std::vector<std::string> foundFiles;
        std::vector<std::string> foundFolders;

        // Validate files and folders and populate respective vectors
        // These can be replaced with actual logic

        if (debug) {
            std::cout << GREEN << "Found files:" << RESET << std::endl;
            for (const auto& path : foundFiles) {
                std::cout << CYAN << path << RESET << std::endl;
            }

            std::cout << GREEN << "Found folders:" << RESET << std::endl;
            for (const auto& folder : foundFolders) {
                std::cout << CYAN << folder << RESET << std::endl;
            }
        }

        // Call other functions with precise order
        deleteRegistryKeys(rkey);
        for (const auto& folder : foundFolders) {
            deleteFolders(foundFolders);
        }
        for (const auto& file : foundFiles) {
            deleteFiles(foundFiles);
        }
        deleteTempFiles();
        emptyRecycleBin();

        std::cout << GREEN << "Cleanup operations completed successfully." << RESET << std::endl;

    }
    catch (const std::exception& e) {
        std::cerr << CYAN << "Error during processing: " << e.what() << RESET << std::endl;
    }

    printBigName(); // Print the cool big name with the message
    std::cin.get();  // Keep the window open
    return 0;

}

