#include "Converter.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include "Logger.h"
#include <shlobj.h>
#include <shobjidl.h>
#include <regex>

bool Converter::ExtractCsoFromVdjShader(const std::wstring& vdjshaderPath, 
                                        const std::wstring& outputPath,
                                        const std::string& customName) {
    // Read the entire ZIP file into memory
    HANDLE hFile = CreateFileW(vdjshaderPath.c_str(), GENERIC_READ, FILE_SHARE_READ, 
                               nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        Logger::LogError(L"Failed to open file: " + vdjshaderPath);
        return false;
    }
    
    DWORD fileSize = GetFileSize(hFile, nullptr);
    if (fileSize == INVALID_FILE_SIZE || fileSize == 0 || fileSize > 16 * 1024 * 1024) {
        Logger::LogError(L"Invalid file size: " + vdjshaderPath);
        CloseHandle(hFile);
        return false;
    }
    
    std::vector<BYTE> zipData(fileSize);
    DWORD bytesRead = 0;
    if (!ReadFile(hFile, zipData.data(), fileSize, &bytesRead, nullptr) || bytesRead != fileSize) {
        Logger::LogError(L"Failed to read file: " + vdjshaderPath);
        CloseHandle(hFile);
        return false;
    }
    CloseHandle(hFile);
    
    Logger::LogInfo(L"File read successfully, size: " + std::to_wstring(fileSize) + L" bytes");
    
    // Simple ZIP parser - find compiled_D11_1.cso entry
    const char* targetName = "compiled_D11_1.cso";
    const size_t targetNameLen = strlen(targetName);
    
    size_t pos = 0;
    int fileCount = 0;
    while (pos < zipData.size() - 30) {
        // Check for local file header signature (0x04034b50)
        if (zipData[pos] == 0x50 && zipData[pos + 1] == 0x4b && 
            zipData[pos + 2] == 0x03 && zipData[pos + 3] == 0x04) {
            // Parse local file header
            uint16_t nameLen = *(uint16_t*)&zipData[pos + 26];
            uint16_t extraLen = *(uint16_t*)&zipData[pos + 28];
            uint32_t compressedSize = *(uint32_t*)&zipData[pos + 18];
            uint16_t compressionMethod = *(uint16_t*)&zipData[pos + 8];
            
            fileCount++;
            
            // Check filename
            const char* filename = (const char*)&zipData[pos + 30];
            if (nameLen == targetNameLen && memcmp(filename, targetName, targetNameLen) == 0) {
                Logger::LogInfo(L"Found compiled_D11_1.cso in ZIP");
                
                // Found the target file
                size_t dataOffset = pos + 30 + nameLen + extraLen;
                
                // For stored (uncompressed) files, just copy the data
                if (compressionMethod == 0) {
                    Logger::LogInfo(L"File is uncompressed, size: " + std::to_wstring(compressedSize) + L" bytes");
                    
                    if (dataOffset + compressedSize <= zipData.size()) {
                        // Determine output filename
                        std::string outputName;
                        if (!customName.empty()) {
                            outputName = SanitizeFilename(customName) + ".cso";
                            int wideSize = MultiByteToWideChar(CP_UTF8, 0, outputName.c_str(), -1, nullptr, 0);
                            std::wstring wideOutputName(wideSize, 0);
                            MultiByteToWideChar(CP_UTF8, 0, outputName.c_str(), -1, &wideOutputName[0], wideSize);
                            wideOutputName.pop_back();
                            Logger::LogInfo(L"Using custom name: " + wideOutputName);
                        } else {
                            // Use source filename without extension
                            std::wstring srcPath = vdjshaderPath;
                            size_t lastSlash = srcPath.find_last_of(L"\\/");
                            std::wstring filename = (lastSlash != std::wstring::npos) 
                                ? srcPath.substr(lastSlash + 1) : srcPath;
                            size_t lastDot = filename.find_last_of(L'.');
                            std::wstring nameWithoutExt = (lastDot != std::wstring::npos)
                                ? filename.substr(0, lastDot) : filename;
                            
                            // Convert to narrow string
                            int size_needed = WideCharToMultiByte(CP_UTF8, 0, nameWithoutExt.c_str(), 
                                                                   -1, nullptr, 0, nullptr, nullptr);
                            std::string narrowName(size_needed, 0);
                            WideCharToMultiByte(CP_UTF8, 0, nameWithoutExt.c_str(), -1, 
                                               &narrowName[0], size_needed, nullptr, nullptr);
                            narrowName.pop_back(); // Remove null terminator
                            outputName = narrowName + ".cso";
                            Logger::LogInfo(L"Using source filename: " + nameWithoutExt + L".cso");
                        }
                        
                        // Build full output path
                        std::wstring fullPath = outputPath;
                        if (!fullPath.empty() && fullPath.back() != L'\\' && fullPath.back() != L'/') {
                            fullPath += L"\\";
                        }
                        
                        // Convert output name to wide string
                        int wideSize = MultiByteToWideChar(CP_UTF8, 0, outputName.c_str(), 
                                                           -1, nullptr, 0);
                        std::wstring wideOutputName(wideSize, 0);
                        MultiByteToWideChar(CP_UTF8, 0, outputName.c_str(), -1, 
                                           &wideOutputName[0], wideSize);
                        wideOutputName.pop_back();
                        
                        fullPath += wideOutputName;
                        
                        Logger::LogInfo(L"Output path: " + fullPath);
                        
                        // Write the CSO file
                        HANDLE hOutFile = CreateFileW(fullPath.c_str(), GENERIC_WRITE, 0, nullptr,
                                                       CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
                        if (hOutFile == INVALID_HANDLE_VALUE) {
                            Logger::LogError(L"Failed to create output file: " + fullPath);
                            return false;
                        }
                        
                        DWORD bytesWritten = 0;
                        BOOL success = WriteFile(hOutFile, zipData.data() + dataOffset, 
                                                compressedSize, &bytesWritten, nullptr);
                        CloseHandle(hOutFile);
                        
                        if (success && bytesWritten == compressedSize) {
                            Logger::LogSuccess(L"Successfully wrote " + std::to_wstring(bytesWritten) + L" bytes");
                            return true;
                        } else {
                            Logger::LogError(L"Failed to write output file, written: " + std::to_wstring(bytesWritten) + 
                                            L", expected: " + std::to_wstring(compressedSize));
                            return false;
                        }
                    } else {
                        Logger::LogError(L"Data offset exceeds file size");
                        return false;
                    }
                } else if (compressionMethod == 8) {
                    // Deflate compression - use tar.exe to extract (built into Windows 10/11)
                    Logger::LogInfo(L"File is compressed with deflate, using tar.exe to extract");
                    
                    // Create a temporary directory for extraction
                    wchar_t tempPath[MAX_PATH];
                    GetTempPathW(MAX_PATH, tempPath);
                    std::wstring tempDir = tempPath;
                    tempDir += L"\\vdjshader_temp_";
                    tempDir += std::to_wstring(GetCurrentProcessId());
                    CreateDirectoryW(tempDir.c_str(), nullptr);
                    
                    // Build tar command to extract
                    std::wstring tarCommand = L"tar -xf \"";
                    tarCommand += vdjshaderPath;
                    tarCommand += L"\" -C \"";
                    tarCommand += tempDir;
                    tarCommand += L"\"";
                    
                    STARTUPINFOW si = {};
                    PROCESS_INFORMATION pi = {};
                    si.cb = sizeof(si);
                    si.dwFlags = STARTF_USESHOWWINDOW;
                    si.wShowWindow = SW_HIDE;
                    
                    if (CreateProcessW(L"C:\\Windows\\System32\\tar.exe", (wchar_t*)tarCommand.c_str(), nullptr, nullptr, FALSE, 
                                       CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
                        WaitForSingleObject(pi.hProcess, 30000); // Wait up to 30 seconds
                        CloseHandle(pi.hProcess);
                        CloseHandle(pi.hThread);
                        
                        // Read the extracted file
                        std::wstring extractedPath = tempDir + L"\\compiled_D11_1.cso";
                        HANDLE hExtracted = CreateFileW(extractedPath.c_str(), GENERIC_READ, FILE_SHARE_READ, 
                                                     nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
                        if (hExtracted != INVALID_HANDLE_VALUE) {
                            DWORD extractedSize = GetFileSize(hExtracted, nullptr);
                            std::vector<BYTE> extractedData(extractedSize);
                            DWORD bytesRead = 0;
                            ReadFile(hExtracted, extractedData.data(), extractedSize, &bytesRead, nullptr);
                            CloseHandle(hExtracted);
                            
                            Logger::LogInfo(L"Extracted " + std::to_wstring(bytesRead) + L" bytes");
                            
                            // Determine output filename
                            std::string outputName;
                            if (!customName.empty()) {
                                outputName = SanitizeFilename(customName) + ".cso";
                                int wideSize = MultiByteToWideChar(CP_UTF8, 0, outputName.c_str(), -1, nullptr, 0);
                                std::wstring wideOutputName(wideSize, 0);
                                MultiByteToWideChar(CP_UTF8, 0, outputName.c_str(), -1, &wideOutputName[0], wideSize);
                                wideOutputName.pop_back();
                                Logger::LogInfo(L"Using custom name: " + wideOutputName);
                            } else {
                                std::wstring srcPath = vdjshaderPath;
                                size_t lastSlash = srcPath.find_last_of(L"\\/");
                                std::wstring filename = (lastSlash != std::wstring::npos) 
                                    ? srcPath.substr(lastSlash + 1) : srcPath;
                                size_t lastDot = filename.find_last_of(L'.');
                                std::wstring nameWithoutExt = (lastDot != std::wstring::npos)
                                    ? filename.substr(0, lastDot) : filename;
                                
                                int size_needed = WideCharToMultiByte(CP_UTF8, 0, nameWithoutExt.c_str(), 
                                                               -1, nullptr, 0, nullptr, nullptr);
                                std::string narrowName(size_needed, 0);
                                WideCharToMultiByte(CP_UTF8, 0, nameWithoutExt.c_str(), -1, 
                                                   &narrowName[0], size_needed, nullptr, nullptr);
                                narrowName.pop_back();
                                outputName = narrowName + ".cso";
                                Logger::LogInfo(L"Using source filename: " + nameWithoutExt + L".cso");
                            }
                            
                            std::wstring fullPath = outputPath;
                            if (!fullPath.empty() && fullPath.back() != L'\\' && fullPath.back() != L'/') {
                                fullPath += L"\\";
                            }
                            
                            int wideSize = MultiByteToWideChar(CP_UTF8, 0, outputName.c_str(), 
                                                               -1, nullptr, 0);
                            std::wstring wideOutputName(wideSize, 0);
                            MultiByteToWideChar(CP_UTF8, 0, outputName.c_str(), -1, 
                                               &wideOutputName[0], wideSize);
                            wideOutputName.pop_back();
                            
                            fullPath += wideOutputName;
                            
                            HANDLE hOutFile = CreateFileW(fullPath.c_str(), GENERIC_WRITE, 0, nullptr,
                                                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
                            if (hOutFile != INVALID_HANDLE_VALUE) {
                                DWORD bytesWritten = 0;
                                WriteFile(hOutFile, extractedData.data(), bytesRead, &bytesWritten, nullptr);
                                CloseHandle(hOutFile);
                                
                                Logger::LogSuccess(L"Successfully wrote " + std::to_wstring(bytesWritten) + L" bytes");
                                
                                // Cleanup temp files
                                DeleteFileW(extractedPath.c_str());
                                RemoveDirectoryW(tempDir.c_str());
                                
                                return true;
                            }
                            CloseHandle(hOutFile);
                        }
                    }
                    
                    // Cleanup temp files
                    DeleteFileW((tempDir + L"\\compiled_D11_1.cso").c_str());
                    RemoveDirectoryW(tempDir.c_str());
                    
                    Logger::LogError(L"Failed to extract using tar.exe");
                    return false;
                } else {
                    Logger::LogError(L"Unsupported compression method: " + std::to_wstring(compressionMethod));
                    break;
                }
            }
            
            // Skip to next file
            pos += 30 + nameLen + extraLen + compressedSize;
        } else {
            pos++;
        }
    }
    
    Logger::LogError(L"Could not find compiled_D11_1.cso in ZIP (scanned " + std::to_wstring(fileCount) + L" files)");
    return false;
}

bool Converter::LoadShaderListXml(const std::wstring& xmlPath, 
                                  std::map<std::string, std::string>& nameMap) {
    // Read XML file
    HANDLE hFile = CreateFileW(xmlPath.c_str(), GENERIC_READ, FILE_SHARE_READ,
                               nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return false;
    
    DWORD fileSize = GetFileSize(hFile, nullptr);
    if (fileSize == INVALID_FILE_SIZE || fileSize == 0) {
        CloseHandle(hFile);
        return false;
    }
    
    std::string xmlContent(fileSize, 0);
    DWORD bytesRead = 0;
    if (!ReadFile(hFile, &xmlContent[0], fileSize, &bytesRead, nullptr) || bytesRead != fileSize) {
        CloseHandle(hFile);
        return false;
    }
    CloseHandle(hFile);
    
    nameMap.clear();
    
    // Simple XML parser - extract filename and prettyname from <shader> elements
    size_t pos = 0;
    while (true) {
        // Find <shader
        size_t shaderStart = xmlContent.find("<shader", pos);
        if (shaderStart == std::string::npos) break;
        
        // Find closing >
        size_t shaderEnd = xmlContent.find(">", shaderStart);
        if (shaderEnd == std::string::npos) break;
        
        // Extract the tag content
        std::string tag = xmlContent.substr(shaderStart, shaderEnd - shaderStart + 1);
        
        // Extract filename
        size_t filenamePos = tag.find("filename=\"");
        if (filenamePos != std::string::npos) {
            filenamePos += 10; // Skip filename="
            size_t filenameEnd = tag.find("\"", filenamePos);
            if (filenameEnd != std::string::npos) {
                std::string filename = tag.substr(filenamePos, filenameEnd - filenamePos);
                
                // Extract prettyname
                size_t prettynamePos = tag.find("prettyname=\"");
                if (prettynamePos != std::string::npos) {
                    prettynamePos += 12; // Skip prettyname="
                    size_t prettynameEnd = tag.find("\"", prettynamePos);
                    if (prettynameEnd != std::string::npos) {
                        std::string prettyname = tag.substr(prettynamePos, prettynameEnd - prettynamePos);
                        // Decode HTML entities
                        size_t entityPos = 0;
                        while ((entityPos = prettyname.find("&amp;", entityPos)) != std::string::npos) {
                            prettyname.replace(entityPos, 5, "&");
                            entityPos += 1;
                        }
                        nameMap[filename] = prettyname;
                    }
                }
            }
        }
        
        pos = shaderEnd + 1;
    }
    
    return !nameMap.empty();
}

std::string Converter::SanitizeFilename(const std::string& name) {
    std::string result = name;
    // Remove invalid filename characters: \ / : * ? " < > |
    const std::string invalidChars = "\\/:*?\"<>|";
    for (char c : invalidChars) {
        result.erase(std::remove(result.begin(), result.end(), c), result.end());
    }
    // Replace spaces with underscores (optional, but cleaner)
    std::replace(result.begin(), result.end(), ' ', '_');
    return result;
}

std::string Converter::GetFilenameWithoutExt(const std::string& filename) {
    size_t lastDot = filename.find_last_of('.');
    if (lastDot != std::string::npos) {
        return filename.substr(0, lastDot);
    }
    return filename;
}

bool Converter::ExtractHlslFromVdjShader(const std::wstring& vdjshaderPath, 
                                         const std::wstring& outputPath,
                                         const std::string& customName) {
    // Use tar.exe to extract shader.json
    wchar_t tempPath[MAX_PATH];
    GetTempPathW(MAX_PATH, tempPath);
    std::wstring tempDir = tempPath;
    tempDir += L"\\vdjshader_hlsl_";
    tempDir += std::to_wstring(GetCurrentProcessId());
    CreateDirectoryW(tempDir.c_str(), nullptr);
    
    // Build tar command to extract only shader.json
    std::wstring tarCommand = L"tar -xf \"";
    tarCommand += vdjshaderPath;
    tarCommand += L"\" -C \"";
    tarCommand += tempDir;
    tarCommand += L"\" shader.json";
    
    STARTUPINFOW si = {};
    PROCESS_INFORMATION pi = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    
    if (CreateProcessW(L"C:\\Windows\\System32\\tar.exe", (wchar_t*)tarCommand.c_str(), nullptr, nullptr, FALSE, 
                       CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, 30000); // Wait up to 30 seconds
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        
        // Read the extracted shader.json
        std::wstring jsonPath = tempDir + L"\\shader.json";
        HANDLE hJson = CreateFileW(jsonPath.c_str(), GENERIC_READ, FILE_SHARE_READ, 
                                    nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hJson != INVALID_HANDLE_VALUE) {
            DWORD jsonSize = GetFileSize(hJson, nullptr);
            std::string jsonData(jsonSize, 0);
            DWORD bytesRead = 0;
            ReadFile(hJson, &jsonData[0], jsonSize, &bytesRead, nullptr);
            CloseHandle(hJson);
            
            // Parse JSON to extract HLSL from prompt field
            size_t promptStart = jsonData.find("\"prompt\":\"");
            if (promptStart != std::string::npos) {
                promptStart += 10; // Skip "prompt":"
                size_t promptEnd = jsonData.find("\",\"", promptStart);
                if (promptEnd == std::string::npos) {
                    promptEnd = jsonData.find("\"}", promptStart);
                }
                if (promptEnd != std::string::npos) {
                    std::string hlsl = jsonData.substr(promptStart, promptEnd - promptStart);
                    
                    // Log first 200 chars of raw extracted content for debugging
                    std::string debugPreview = hlsl.substr(0, 200);
                    int debugSize = MultiByteToWideChar(CP_UTF8, 0, debugPreview.c_str(), -1, nullptr, 0);
                    std::wstring debugWide(debugSize, 0);
                    MultiByteToWideChar(CP_UTF8, 0, debugPreview.c_str(), -1, &debugWide[0], debugSize);
                    Logger::LogInfo(L"Raw extracted content preview: " + debugWide);
                    
                    // Unescape JSON - order matters!
                    size_t escapePos = 0;
                    // Convert \n to newline FIRST (before any other replacements)
                    while ((escapePos = hlsl.find("\\n", escapePos)) != std::string::npos) {
                        hlsl.replace(escapePos, 2, "\n");
                        escapePos += 1;
                    }
                    // Then convert \" to "
                    while ((escapePos = hlsl.find("\\\"", escapePos)) != std::string::npos) {
                        hlsl.replace(escapePos, 2, "\"");
                        escapePos += 1;
                    }
                    // Handle the specific case of \\\/\\\/ (double backslash + slash)
                    // This becomes \/ after JSON parsing, which should be //
                    while ((escapePos = hlsl.find("\\/\\/", escapePos)) != std::string::npos) {
                        hlsl.replace(escapePos, 4, "//");
                        escapePos += 2;
                    }
                    // Then convert any remaining \/ to /
                    while ((escapePos = hlsl.find("\\/", escapePos)) != std::string::npos) {
                        hlsl.replace(escapePos, 2, "/");
                        escapePos += 1;
                    }
                    // Then convert double backslash to single backslash
                    while ((escapePos = hlsl.find("\\\\", escapePos)) != std::string::npos) {
                        hlsl.replace(escapePos, 2, "\\");
                        escapePos += 1;
                    }
                    
                    // Log first 200 chars after unescaping
                    debugPreview = hlsl.substr(0, 200);
                    debugSize = MultiByteToWideChar(CP_UTF8, 0, debugPreview.c_str(), -1, nullptr, 0);
                    debugWide = std::wstring(debugSize, 0);
                    MultiByteToWideChar(CP_UTF8, 0, debugPreview.c_str(), -1, &debugWide[0], debugSize);
                    Logger::LogInfo(L"After unescaping preview: " + debugWide);
                    
                    // Validate that extracted content looks like HLSL code
                    // Check for actual shader syntax patterns (not just keywords)
                    bool hasShaderSyntax = (hlsl.find("void main") != std::string::npos ||
                                           hlsl.find("void mainImage") != std::string::npos ||
                                           hlsl.find("float4 PSMain") != std::string::npos ||
                                           hlsl.find("cbuffer") != std::string::npos ||
                                           (hlsl.find("float2") != std::string::npos && hlsl.find(";") != std::string::npos));
                    
                    // Also check for common AI prompt indicators
                    bool hasAIPromptIndicators = (hlsl.find("Shader") != std::string::npos && 
                                                 hlsl.find("effect") != std::string::npos) ||
                                                hlsl.find("create") != std::string::npos ||
                                                hlsl.find("make") != std::string::npos ||
                                                hlsl.find("\\u") != std::string::npos; // Unicode escapes
                    
                    if (!hasShaderSyntax || hasAIPromptIndicators) {
                        Logger::LogError(L"shader.json does not contain valid HLSL code (appears to be an AI prompt)");
                        DeleteFileW(jsonPath.c_str());
                        RemoveDirectoryW(tempDir.c_str());
                        return false;
                    }
                    
                    // Determine output filename
                    std::string outputName;
                    if (!customName.empty()) {
                        outputName = SanitizeFilename(customName) + ".hlsl";
                    } else {
                        std::wstring srcPath = vdjshaderPath;
                        size_t lastSlash = srcPath.find_last_of(L"\\/");
                        std::wstring filename = (lastSlash != std::wstring::npos) 
                            ? srcPath.substr(lastSlash + 1) : srcPath;
                        size_t lastDot = filename.find_last_of(L'.');
                        std::wstring nameWithoutExt = (lastDot != std::wstring::npos)
                            ? filename.substr(0, lastDot) : filename;
                        
                        int size_needed = WideCharToMultiByte(CP_UTF8, 0, nameWithoutExt.c_str(), 
                                                               -1, nullptr, 0, nullptr, nullptr);
                        std::string narrowName(size_needed, 0);
                        WideCharToMultiByte(CP_UTF8, 0, nameWithoutExt.c_str(), -1, 
                                           &narrowName[0], size_needed, nullptr, nullptr);
                        narrowName.pop_back();
                        outputName = narrowName + ".hlsl";
                    }
                    
                    std::wstring fullPath = outputPath;
                    if (!fullPath.empty() && fullPath.back() != L'\\' && fullPath.back() != L'/') {
                        fullPath += L"\\";
                    }
                    
                    int wideSize = MultiByteToWideChar(CP_UTF8, 0, outputName.c_str(), 
                                                       -1, nullptr, 0);
                    std::wstring wideOutputName(wideSize, 0);
                    MultiByteToWideChar(CP_UTF8, 0, outputName.c_str(), -1, 
                                       &wideOutputName[0], wideSize);
                    wideOutputName.pop_back();
                    
                    fullPath += wideOutputName;
                    
                    HANDLE hOutFile = CreateFileW(fullPath.c_str(), GENERIC_WRITE, 0, nullptr,
                                                   CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
                    if (hOutFile != INVALID_HANDLE_VALUE) {
                        DWORD bytesWritten = 0;
                        WriteFile(hOutFile, hlsl.c_str(), (DWORD)hlsl.size(), &bytesWritten, nullptr);
                        CloseHandle(hOutFile);
                        
                        // Cleanup temp files
                        DeleteFileW(jsonPath.c_str());
                        RemoveDirectoryW(tempDir.c_str());
                        
                        Logger::LogSuccess(L"Extracted HLSL: " + fullPath);
                        return true;
                    }
                }
            }
            CloseHandle(hJson);
        }
    }
    
    // Cleanup temp files
    DeleteFileW((tempDir + L"\\shader.json").c_str());
    RemoveDirectoryW(tempDir.c_str());
    
    Logger::LogError(L"Could not extract shader.json from .vdjshader file");
    return false;
}

std::string Converter::ConvertGlslToHlsl(const std::string& glsl) {
    std::string hlsl = glsl;
    
    // Add TellyMedia constant buffer at the beginning
    std::string shaderCB = 
        "cbuffer ShaderCB : register(b0)\n"
        "{\n"
        "    float2 iResolution;\n"
        "    float  iTime;\n"
        "    float  iBeat;\n"
        "    float  iLevel;\n"
        "    float  iBass, iMid, iTreble;\n"
        "    float  iBpm;\n"
        "    float  iSongPosBeats;\n"
        "    float  _pad;\n"
        "    float panelX, panelY, panelW, panelH;\n"
        "};\n\n";
    
    // Replace VDJ audio helpers with ShaderCB parameters
    hlsl = std::regex_replace(hlsl, std::regex(R"(float getBass\(\)\s*\{\s*return\s*texture\(iChannel0,\s*vec2\([^)]*\)\)\.x;\s*\})"), "float getBass() { return iBass; }");
    hlsl = std::regex_replace(hlsl, std::regex(R"(float getMid\(\)\s*\{\s*return\s*texture\(iChannel0,\s*vec2\([^)]*\)\)\.x;\s*\})"), "float getMid() { return iMid; }");
    hlsl = std::regex_replace(hlsl, std::regex(R"(float getHigh\(\)\s*\{\s*return\s*texture\(iChannel0,\s*vec2\([^)]*\)\)\.x;\s*\})"), "float getHigh() { return iTreble; }");
    hlsl = std::regex_replace(hlsl, std::regex(R"(float getBeat\(\)\s*\{\s*return\s*texture\(iChannel0,\s*vec2\([^)]*\)\)\.x;\s*\})"), "float getBeat() { return iBeat; }");
    
    // Remove iChannel0 references
    hlsl = std::regex_replace(hlsl, std::regex(R"(//\s*iChannel0:\s*[^\n]*\n)"), "");
    
    // GLSL to HLSL type conversions
    hlsl = std::regex_replace(hlsl, std::regex(R"(\bvec2\b)"), "float2");
    hlsl = std::regex_replace(hlsl, std::regex(R"(\bvec3\b)"), "float3");
    hlsl = std::regex_replace(hlsl, std::regex(R"(\bvec4\b)"), "float4");
    hlsl = std::regex_replace(hlsl, std::regex(R"(\bmat2\b)"), "float2x2");
    hlsl = std::regex_replace(hlsl, std::regex(R"(\bmat3\b)"), "float3x3");
    hlsl = std::regex_replace(hlsl, std::regex(R"(\bmat4\b)"), "float4x4");
    
    // GLSL to HLSL function conversions
    hlsl = std::regex_replace(hlsl, std::regex(R"(\bfract\b)"), "frac");
    hlsl = std::regex_replace(hlsl, std::regex(R"(\bmix\b)"), "lerp");
    hlsl = std::regex_replace(hlsl, std::regex(R"(\bdistance\b)"), "length");
    
    // Insert ShaderCB at the beginning (after comments)
    size_t insertPos = 0;
    while (insertPos < hlsl.size() && (hlsl[insertPos] == '/' || hlsl[insertPos] == '\n' || hlsl[insertPos] == ' ')) {
        insertPos++;
    }
    hlsl.insert(insertPos, shaderCB);
    
    // Replace mainImage() with PSMain if it exists (Shadertoy style)
    // Pattern: void mainImage(out float4 fragColor, in float2 fragCoord)
    std::regex mainImagePattern(R"(\bvoid\s+mainImage\s*\(\s*out\s+float4\s+(\w+),\s*in\s+float2\s+(\w+)\s*\))");
    std::smatch match;
    std::string hlslCopy = hlsl;
    if (std::regex_search(hlslCopy, match, mainImagePattern)) {
        std::string fragColorVar = match[1].str();
        std::string fragCoordVar = match[2].str();
        
        // Replace function signature
        hlsl = std::regex_replace(hlsl, mainImagePattern, "float4 PSMain(float4 pos : SV_POSITION, float2 uv : TEXCOORD0) : SV_Target");
        
        // Replace fragCoord.xy with uv * iResolution
        hlsl = std::regex_replace(hlsl, std::regex(fragCoordVar + R"(\.xy)"), "uv * iResolution");
        
        // Replace fragCoord with uv * iResolution (if used without .xy)
        hlsl = std::regex_replace(hlsl, std::regex(R"(\b)" + fragCoordVar + R"(\b)"), "uv * iResolution");
        
        // Replace fragColor = with return
        hlsl = std::regex_replace(hlsl, std::regex(fragColorVar + R"(\s*=)"), "return");
        
        // Remove any remaining fragColor variable declarations
        hlsl = std::regex_replace(hlsl, std::regex(R"(\bfloat4\s+)" + fragColorVar + R"(\s*;)"), "");
    }
    
    // Replace main() with PSMain if it exists
    hlsl = std::regex_replace(hlsl, std::regex(R"(\bvoid\s+main\s*\(\s*[^)]*\s*\))"), "float4 PSMain(float4 pos : SV_POSITION, float2 uv : TEXCOORD0) : SV_Target");
    
    // Remove incorrect PSMain wrapper if it was added
    hlsl = std::regex_replace(hlsl, std::regex(R"(float4 PSMain\(float4 pos : SV_POSITION, float2 uv : TEXCOORD0\) : SV_Target\s*\{\s*return main\(uv\);\s*\})"), "");
    
    // If no PSMain function, add wrapper
    if (hlsl.find("PSMain") == std::string::npos) {
        hlsl += "\n\nfloat4 PSMain(float4 pos : SV_POSITION, float2 uv : TEXCOORD0) : SV_Target\n{\n    float4 color;\n    mainImage(color, uv * iResolution);\n    return color;\n}\n";
    }
    
    return hlsl;
}

bool Converter::CompileHlslToCso(const std::wstring& hlslPath, const std::wstring& csoPath) {
    // Find fxc.exe in Windows SDK
    wchar_t programFiles[MAX_PATH];
    ExpandEnvironmentStringsW(L"%ProgramFiles(x86)%", programFiles, MAX_PATH);
    
    std::wstring fxcPath = programFiles;
    fxcPath += L"\\Windows Kits\\10\\bin\\10.0.26100.0\\x64\\fxc.exe";
    
    // Try alternate paths if not found
    HANDLE hTest = CreateFileW(fxcPath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hTest == INVALID_HANDLE_VALUE) {
        fxcPath = programFiles;
        fxcPath += L"\\Windows Kits\\10\\bin\\x64\\fxc.exe";
        hTest = CreateFileW(fxcPath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    }
    if (hTest == INVALID_HANDLE_VALUE) {
        Logger::LogError(L"Could not find fxc.exe in Windows Kits");
        return false;
    }
    CloseHandle(hTest);
    
    // Build command: fxc /T ps_5_0 /E PSMain input.hlsl /Fo output.cso
    std::wstring command = L"\"";
    command += fxcPath;
    command += L"\" /T ps_5_0 /E PSMain \"";
    command += hlslPath;
    command += L"\" /Fo \"";
    command += csoPath;
    command += L"\"";
    
    // Create pipes for stderr capture
    SECURITY_ATTRIBUTES sa = {};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    
    HANDLE hReadPipe, hWritePipe;
    if (!CreatePipe(&hReadPipe, &hWritePipe, &sa, 0)) {
        Logger::LogError(L"Failed to create pipe for fxc.exe output");
        return false;
    }
    
    SetHandleInformation(hReadPipe, HANDLE_FLAG_INHERIT, 0);
    
    STARTUPINFOW si = {};
    PROCESS_INFORMATION pi = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
    si.wShowWindow = SW_HIDE;
    si.hStdError = hWritePipe;
    si.hStdOutput = hWritePipe;
    
    if (CreateProcessW(nullptr, (wchar_t*)command.c_str(), nullptr, nullptr, TRUE, 
                       CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        CloseHandle(hWritePipe);
        
        // Read output
        char buffer[4096];
        std::string output;
        DWORD bytesRead;
        while (ReadFile(hReadPipe, buffer, sizeof(buffer) - 1, &bytesRead, nullptr) && bytesRead > 0) {
            buffer[bytesRead] = 0;
            output += buffer;
        }
        CloseHandle(hReadPipe);
        
        WaitForSingleObject(pi.hProcess, 30000); // Wait up to 30 seconds
        DWORD exitCode = 0;
        GetExitCodeProcess(pi.hProcess, &exitCode);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        
        if (exitCode == 0) {
            Logger::LogSuccess(L"Compiled HLSL to CSO: " + csoPath);
            return true;
        } else {
            // Convert error output to wide string for logging
            int size_needed = MultiByteToWideChar(CP_UTF8, 0, output.c_str(), -1, nullptr, 0);
            std::wstring wideOutput(size_needed, 0);
            MultiByteToWideChar(CP_UTF8, 0, output.c_str(), -1, &wideOutput[0], size_needed);
            Logger::LogError(L"fxc.exe error: " + wideOutput);
            return false;
        }
    }
    
    CloseHandle(hWritePipe);
    CloseHandle(hReadPipe);
    Logger::LogError(L"Failed to run fxc.exe");
    return false;
}
