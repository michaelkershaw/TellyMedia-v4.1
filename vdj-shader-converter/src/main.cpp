#include <windows.h>
#include <commdlg.h>
#include <shlobj.h>
#include <vector>
#include <string>
#include <map>
#include "Converter.h"
#include "IniHandler.h"
#include "Logger.h"

#pragma comment(lib, "comctl32.lib")

// Global state
struct AppState {
    HWND hWnd;
    HWND hFilesList;
    HWND hOutputEdit;
    HWND hProgress;
    HWND hStatus;
    HWND hXmlPathEdit;
    std::vector<std::wstring> filesToConvert;
    std::map<std::string, std::string> shaderNameMap;
    std::wstring xmlPath;
    bool isConverting;
    int convertMode; // 0 = HLSL only, 1 = Convert & Recompile
} g_state;

// Control IDs
#define ID_SELECT_FILES    1001
#define ID_SELECT_FOLDER  1002
#define ID_SELECT_XML     1003
#define ID_BROWSE_OUTPUT  1004
#define ID_CONVERT         1005
#define ID_FILES_LIST     1006
#define ID_OUTPUT_EDIT     1007
#define ID_PROGRESS        1008
#define ID_STATUS          1009
#define ID_XML_PATH_EDIT   1010
#define ID_VIEW_LOG        1011
#define ID_MODE_HLSL       1012
#define ID_MODE_CONVERT    1013

// Window class name
const wchar_t CLASS_NAME[] = L"VDJShaderConverterWindow";

void UpdateStatus(const std::wstring& text) {
    SetWindowTextW(g_state.hStatus, text.c_str());
}

void AddFileToList(const std::wstring& filepath) {
    // Get filename without path
    size_t lastSlash = filepath.find_last_of(L"\\/");
    std::wstring filename = (lastSlash != std::wstring::npos) 
        ? filepath.substr(lastSlash + 1) : filepath;
    
    // Add to listbox
    SendMessageW(g_state.hFilesList, LB_ADDSTRING, 0, (LPARAM)filename.c_str());
    g_state.filesToConvert.push_back(filepath);
}

void ClearFileList() {
    SendMessageW(g_state.hFilesList, LB_RESETCONTENT, 0, 0);
    g_state.filesToConvert.clear();
}

void OnSelectFiles() {
    OPENFILENAMEW ofn = {};
    wchar_t fileBuffer[65536] = L"";
    
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_state.hWnd;
    ofn.lpstrFilter = L"VDJ Shader Files (*.vdjshader)\0*.vdjshader\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = fileBuffer;
    ofn.nMaxFile = 65536;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_ALLOWMULTISELECT | OFN_EXPLORER;
    
    if (GetOpenFileNameW(&ofn)) {
        Logger::LogInfo(L"File selection dialog opened");
        
        // Handle multiple file selection
        std::wstring directory = fileBuffer;
        size_t nullPos = directory.find(L'\0');
        
        if (nullPos == std::wstring::npos) {
            // Single file selected - only add .vdjshader files
            if (std::wstring(fileBuffer).find(L".vdjshader") != std::wstring::npos) {
                AddFileToList(fileBuffer);
                Logger::LogInfo(L"Added file: " + std::wstring(fileBuffer));
            }
        } else {
            // Multiple files selected - only add .vdjshader files
            directory = directory.substr(0, nullPos);
            const wchar_t* ptr = fileBuffer + nullPos + 1;
            
            int count = 0;
            while (*ptr) {
                std::wstring filepath = directory + L"\\" + ptr;
                if (filepath.find(L".vdjshader") != std::wstring::npos) {
                    AddFileToList(filepath);
                    Logger::LogInfo(L"Added file: " + filepath);
                    count++;
                }
                ptr += wcslen(ptr) + 1;
            }
            wchar_t logMsg[256];
            swprintf_s(logMsg, L"Added %d files from selection", count);
            Logger::LogInfo(logMsg);
        }
        UpdateStatus(L"Files added. Ready to convert.");
    } else {
        Logger::LogInfo(L"File selection dialog cancelled");
    }
}

void OnSelectFolder() {
    BROWSEINFOW bi = {};
    wchar_t path[MAX_PATH];
    
    bi.hwndOwner = g_state.hWnd;
    bi.pszDisplayName = path;
    bi.lpszTitle = L"Select Folder with .vdjshader Files";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    
    LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
    if (pidl) {
        if (SHGetPathFromIDListW(pidl, path)) {
            Logger::LogInfo(L"Folder selected: " + std::wstring(path));
            
            // Find all .vdjshader files in the folder
            std::wstring searchPath = path;
            searchPath += L"\\*.vdjshader";
            
            WIN32_FIND_DATAW fd;
            HANDLE hFind = FindFirstFileW(searchPath.c_str(), &fd);
            
            if (hFind != INVALID_HANDLE_VALUE) {
                int count = 0;
                do {
                    if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                        std::wstring filepath = path;
                        filepath += L"\\";
                        filepath += fd.cFileName;
                        AddFileToList(filepath);
                        Logger::LogInfo(L"Added file from folder: " + std::wstring(fd.cFileName));
                        count++;
                    }
                } while (FindNextFileW(hFind, &fd));
                FindClose(hFind);
                
                wchar_t status[256];
                swprintf_s(status, L"Added %d files from folder.", count);
                UpdateStatus(status);
                
                wchar_t logMsg[256];
                swprintf_s(logMsg, L"Total files added from folder: %d", count);
                Logger::LogInfo(logMsg);
            } else {
                Logger::LogError(L"No .vdjshader files found in folder: " + std::wstring(path));
            }
        }
        CoTaskMemFree(pidl);
    } else {
        Logger::LogInfo(L"Folder selection dialog cancelled");
    }
}

void OnSelectXml() {
    OPENFILENAMEW ofn = {};
    wchar_t fileBuffer[MAX_PATH] = L"";
    
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_state.hWnd;
    ofn.lpstrFilter = L"XML Files (*.xml)\0*.xml\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = fileBuffer;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    
    if (GetOpenFileNameW(&ofn)) {
        g_state.xmlPath = fileBuffer;
        SetWindowTextW(g_state.hXmlPathEdit, fileBuffer);
        
        Logger::LogInfo(L"XML file selected: " + std::wstring(fileBuffer));
        
        // Load the XML
        if (Converter::LoadShaderListXml(g_state.xmlPath, g_state.shaderNameMap)) {
            wchar_t logMsg[256];
            swprintf_s(logMsg, L"Loaded %d shader names from XML", (int)g_state.shaderNameMap.size());
            Logger::LogSuccess(logMsg);
            UpdateStatus(L"Shader names loaded from XML.");
        } else {
            Logger::LogError(L"Failed to load shader names from XML: " + std::wstring(fileBuffer));
            UpdateStatus(L"Failed to load shader names from XML.");
        }
    } else {
        Logger::LogInfo(L"XML file selection dialog cancelled");
    }
}

void OnBrowseOutput() {
    BROWSEINFOW bi = {};
    wchar_t path[MAX_PATH];
    
    bi.hwndOwner = g_state.hWnd;
    bi.pszDisplayName = path;
    bi.lpszTitle = L"Select Output Directory";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    
    LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
    if (pidl) {
        if (SHGetPathFromIDListW(pidl, path)) {
            SetWindowTextW(g_state.hOutputEdit, path);
            Logger::LogInfo(L"Output directory set to: " + std::wstring(path));
        }
        CoTaskMemFree(pidl);
    } else {
        Logger::LogInfo(L"Output directory selection cancelled");
    }
}

void OnConvert() {
    if (g_state.filesToConvert.empty()) {
        UpdateStatus(L"No files to convert.");
        Logger::LogError(L"Convert attempted with no files in list");
        return;
    }
    
    // Get output directory
    wchar_t outputDir[MAX_PATH];
    GetWindowTextW(g_state.hOutputEdit, outputDir, MAX_PATH);
    
    if (wcslen(outputDir) == 0) {
        UpdateStatus(L"Please select an output directory.");
        Logger::LogError(L"Convert attempted with no output directory");
        return;
    }
    
    Logger::LogInfo(L"Starting conversion process");
    Logger::LogInfo(L"Output directory: " + std::wstring(outputDir));
    
    // Save output directory to INI
    std::wstring iniPath = IniHandler::GetDefaultIniPath();
    IniHandler::SaveOutputDirectory(iniPath, outputDir);
    
    g_state.isConverting = true;
    EnableWindow(GetDlgItem(g_state.hWnd, ID_CONVERT), FALSE);
    
    int successCount = 0;
    int failCount = 0;
    int total = (int)g_state.filesToConvert.size();
    
    wchar_t startMsg[256];
    swprintf_s(startMsg, L"Starting conversion of %d files", total);
    Logger::LogInfo(startMsg);
    
    for (size_t i = 0; i < g_state.filesToConvert.size(); i++) {
        // Update progress
        SendMessageW(g_state.hProgress, PBM_SETPOS, (WPARAM)((i * 100) / total), 0);
        
        // Get shader name from XML if available
        std::string customName;
        std::wstring filepath = g_state.filesToConvert[i];
        
        // Extract filename without extension
        size_t lastSlash = filepath.find_last_of(L"\\/");
        std::wstring filename = (lastSlash != std::wstring::npos) 
            ? filepath.substr(lastSlash + 1) : filepath;
        size_t lastDot = filename.find_last_of(L'.');
        std::wstring nameWithoutExt = (lastDot != std::wstring::npos)
            ? filename.substr(0, lastDot) : filename;
        
        // Convert to narrow string for map lookup
        int size_needed = WideCharToMultiByte(CP_UTF8, 0, nameWithoutExt.c_str(), 
                                               -1, nullptr, 0, nullptr, nullptr);
        std::string narrowName(size_needed, 0);
        WideCharToMultiByte(CP_UTF8, 0, nameWithoutExt.c_str(), -1, 
                           &narrowName[0], size_needed, nullptr, nullptr);
        narrowName.pop_back();
        
        // Look up in shader name map
        auto it = g_state.shaderNameMap.find(narrowName);
        if (it != g_state.shaderNameMap.end()) {
            customName = it->second;
        }
        
        bool fileSuccess = false;
        
        // Mode 0: Extract HLSL only
        if (g_state.convertMode == 0) {
            if (Converter::ExtractHlslFromVdjShader(filepath, outputDir, customName)) {
                fileSuccess = true;
                Logger::LogSuccess(L"Extracted HLSL: " + filename);
            } else {
                Logger::LogError(L"Failed to extract HLSL: " + filename);
            }
        }
        // Mode 1: Extract HLSL, convert, and recompile
        else if (g_state.convertMode == 1) {
            // First extract HLSL
            std::wstring hlslPath = outputDir;
            if (!hlslPath.empty() && hlslPath.back() != L'\\' && hlslPath.back() != L'/') {
                hlslPath += L"\\";
            }
            
            std::string hlslName;
            if (!customName.empty()) {
                hlslName = Converter::SanitizeFilename(customName) + ".hlsl";
            } else {
                hlslName = narrowName + ".hlsl";
            }
            
            int wideSize = MultiByteToWideChar(CP_UTF8, 0, hlslName.c_str(), -1, nullptr, 0);
            std::wstring wideHlslName(wideSize, 0);
            MultiByteToWideChar(CP_UTF8, 0, hlslName.c_str(), -1, &wideHlslName[0], wideSize);
            wideHlslName.pop_back();
            
            hlslPath += wideHlslName;
            
            if (Converter::ExtractHlslFromVdjShader(filepath, outputDir, customName)) {
                // Read the extracted HLSL
                HANDLE hHlsl = CreateFileW(hlslPath.c_str(), GENERIC_READ, FILE_SHARE_READ, 
                                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
                if (hHlsl != INVALID_HANDLE_VALUE) {
                    DWORD hlslSize = GetFileSize(hHlsl, nullptr);
                    std::string hlslContent(hlslSize, 0);
                    DWORD bytesRead = 0;
                    ReadFile(hHlsl, &hlslContent[0], hlslSize, &bytesRead, nullptr);
                    CloseHandle(hHlsl);
                    
                    // Convert GLSL to HLSL
                    std::string convertedHlsl = Converter::ConvertGlslToHlsl(hlslContent);
                    
                    // Write converted HLSL
                    HANDLE hOut = CreateFileW(hlslPath.c_str(), GENERIC_WRITE, 0, nullptr,
                                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
                    if (hOut != INVALID_HANDLE_VALUE) {
                        WriteFile(hOut, convertedHlsl.c_str(), (DWORD)convertedHlsl.size(), &bytesRead, nullptr);
                        CloseHandle(hOut);
                        
                        Logger::LogInfo(L"Converted GLSL to HLSL: " + filename);
                        
                        // Compile to CSO
                        std::wstring csoPath = outputDir;
                        if (!csoPath.empty() && csoPath.back() != L'\\' && csoPath.back() != L'/') {
                            csoPath += L"\\";
                        }
                        
                        std::string csoName;
                        if (!customName.empty()) {
                            csoName = Converter::SanitizeFilename(customName) + ".cso";
                        } else {
                            csoName = narrowName + ".cso";
                        }
                        
                        wideSize = MultiByteToWideChar(CP_UTF8, 0, csoName.c_str(), -1, nullptr, 0);
                        std::wstring wideCsoName(wideSize, 0);
                        MultiByteToWideChar(CP_UTF8, 0, csoName.c_str(), -1, &wideCsoName[0], wideSize);
                        wideCsoName.pop_back();
                        
                        csoPath += wideCsoName;
                        
                        if (Converter::CompileHlslToCso(hlslPath, csoPath)) {
                            fileSuccess = true;
                            Logger::LogSuccess(L"Converted & compiled: " + filename);
                        } else {
                            Logger::LogError(L"Compilation failed: " + filename);
                        }
                    }
                }
            } else {
                Logger::LogError(L"Failed to extract HLSL for conversion: " + filename);
            }
        }
        
        if (fileSuccess) {
            successCount++;
        } else {
            failCount++;
        }
        
        // Process window messages to keep UI responsive
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    
    // Final progress update
    SendMessageW(g_state.hProgress, PBM_SETPOS, 100, 0);
    
    // Show result
    wchar_t result[256];
    swprintf_s(result, L"Conversion complete: %d succeeded, %d failed.", successCount, failCount);
    UpdateStatus(result);
    
    wchar_t logResult[256];
    swprintf_s(logResult, L"Conversion finished: %d succeeded, %d failed", successCount, failCount);
    Logger::LogInfo(logResult);
    
    g_state.isConverting = false;
    EnableWindow(GetDlgItem(g_state.hWnd, ID_CONVERT), TRUE);
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_CREATE: {
            g_state.hWnd = hWnd;
            g_state.convertMode = 1; // Default to Convert & Recompile
            
            // Create controls
            CreateWindowW(L"BUTTON", L"Select Files", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                         20, 20, 120, 30, hWnd, (HMENU)ID_SELECT_FILES, nullptr, nullptr);
            
            CreateWindowW(L"BUTTON", L"Select Folder", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                         150, 20, 120, 30, hWnd, (HMENU)ID_SELECT_FOLDER, nullptr, nullptr);
            
            CreateWindowW(L"BUTTON", L"Load shaderslist.xml", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                         280, 20, 150, 30, hWnd, (HMENU)ID_SELECT_XML, nullptr, nullptr);
            
            // Conversion mode radio buttons
            CreateWindowW(L"STATIC", L"Mode:", WS_CHILD | WS_VISIBLE,
                         20, 60, 40, 20, hWnd, nullptr, nullptr, nullptr);
            
            CreateWindowW(L"BUTTON", L"Extract HLSL", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
                         60, 60, 100, 20, hWnd, (HMENU)ID_MODE_HLSL, nullptr, nullptr);
            
            CreateWindowW(L"BUTTON", L"Convert & Recompile", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
                         170, 60, 140, 20, hWnd, (HMENU)ID_MODE_CONVERT, nullptr, nullptr);
            
            // Set default radio button
            SendMessageW(GetDlgItem(hWnd, ID_MODE_CONVERT), BM_SETCHECK, BST_CHECKED, 0);
            
            CreateWindowW(L"STATIC", L"Output Directory:", WS_CHILD | WS_VISIBLE,
                         20, 90, 100, 20, hWnd, nullptr, nullptr, nullptr);
            
            g_state.hOutputEdit = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_READONLY,
                                               120, 88, 300, 24, hWnd, (HMENU)ID_OUTPUT_EDIT, nullptr, nullptr);
            
            CreateWindowW(L"BUTTON", L"Browse...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                         430, 68, 80, 24, hWnd, (HMENU)ID_BROWSE_OUTPUT, nullptr, nullptr);
            
            CreateWindowW(L"STATIC", L"shaderslist.xml:", WS_CHILD | WS_VISIBLE,
                         20, 110, 100, 20, hWnd, nullptr, nullptr, nullptr);
            
            g_state.hXmlPathEdit = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_READONLY,
                                                120, 108, 300, 24, hWnd, (HMENU)ID_XML_PATH_EDIT, nullptr, nullptr);
            
            CreateWindowW(L"STATIC", L"Files to Convert:", WS_CHILD | WS_VISIBLE,
                         20, 170, 100, 20, hWnd, nullptr, nullptr, nullptr);
            
            g_state.hFilesList = CreateWindowW(L"LISTBOX", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | 
                                               WS_VSCROLL | LBS_SORT,
                                               20, 195, 350, 200, hWnd, (HMENU)ID_FILES_LIST, nullptr, nullptr);
            
            g_state.hProgress = CreateWindowW(L"PROGRESS_CLASS", L"", WS_CHILD | WS_VISIBLE | PBS_SMOOTH,
                                             20, 410, 350, 20, hWnd, (HMENU)ID_PROGRESS, nullptr, nullptr);
            
            CreateWindowW(L"BUTTON", L"CONVERT", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                         390, 195, 120, 40, hWnd, (HMENU)ID_CONVERT, nullptr, nullptr);
            
            g_state.hStatus = CreateWindowW(L"STATIC", L"Ready", WS_CHILD | WS_VISIBLE,
                                          20, 440, 490, 20, hWnd, (HMENU)ID_STATUS, nullptr, nullptr);
            
            CreateWindowW(L"BUTTON", L"View Log", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                         390, 240, 120, 30, hWnd, (HMENU)ID_VIEW_LOG, nullptr, nullptr);
            
            // Load saved output directory
            std::wstring iniPath = IniHandler::GetDefaultIniPath();
            std::wstring savedDir = IniHandler::GetOutputDirectory(iniPath);
            if (!savedDir.empty()) {
                SetWindowTextW(g_state.hOutputEdit, savedDir.c_str());
            }
            
            break;
        }
        
        case WM_COMMAND: {
            switch (LOWORD(wParam)) {
                case ID_SELECT_FILES:
                    OnSelectFiles();
                    break;
                case ID_SELECT_FOLDER:
                    OnSelectFolder();
                    break;
                case ID_SELECT_XML:
                    OnSelectXml();
                    break;
                case ID_BROWSE_OUTPUT:
                    OnBrowseOutput();
                    break;
                case ID_MODE_HLSL:
                    g_state.convertMode = 0;
                    Logger::LogInfo(L"Mode set to: Extract HLSL only");
                    break;
                case ID_MODE_CONVERT:
                    g_state.convertMode = 1;
                    Logger::LogInfo(L"Mode set to: Convert & Recompile");
                    break;
                case ID_CONVERT:
                    OnConvert();
                    break;
                case ID_VIEW_LOG:
                    ShellExecuteW(nullptr, L"open", Logger::GetLogFilePath().c_str(), nullptr, nullptr, SW_SHOW);
                    break;
            }
            break;
        }
        
        case WM_DESTROY:
            PostQuitMessage(0);
            break;
        
        default:
            return DefWindowProcW(hWnd, message, wParam, lParam);
    }
    return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    // Initialize logger
    Logger::Initialize();
    Logger::LogInfo(L"VDJShader Converter starting");
    
    // Initialize COM
    CoInitialize(nullptr);
    
    // Initialize common controls
    INITCOMMONCONTROLSEX icc = {};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_PROGRESS_CLASS;
    InitCommonControlsEx(&icc);
    
    // Register window class
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = CLASS_NAME;
    
    RegisterClassExW(&wc);
    
    // Create window
    HWND hWnd = CreateWindowExW(0, CLASS_NAME, L"VDJShader to CSO Converter",
                                WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                                CW_USEDEFAULT, CW_USEDEFAULT, 540, 500,
                                nullptr, nullptr, hInstance, nullptr);
    
    if (!hWnd) {
        Logger::LogError(L"Failed to create main window");
        return 1;
    }
    
    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);
    
    // Message loop
    MSG msg = {};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    
    Logger::LogInfo(L"VDJShader Converter shutting down");
    CoUninitialize();
    return (int)msg.wParam;
}
