#pragma once

#include <string>
#include <vector>
#include <map>
#include <windows.h>

struct ShaderInfo {
    std::string filename;
    std::string prettyname;
};

class Converter {
public:
    // Extract CSO from a single .vdjshader file
    static bool ExtractCsoFromVdjShader(const std::wstring& vdjshaderPath, 
                                        const std::wstring& outputPath,
                                        const std::string& customName = "");

    // Extract HLSL source from shader.json in .vdjshader file
    static bool ExtractHlslFromVdjShader(const std::wstring& vdjshaderPath, 
                                         const std::wstring& outputPath,
                                         const std::string& customName = "");

    // Convert VDJ GLSL to TellyMedia HLSL
    static std::string ConvertGlslToHlsl(const std::string& glsl);

    // Compile HLSL to CSO using fxc.exe
    static bool CompileHlslToCso(const std::wstring& hlslPath, 
                                  const std::wstring& csoPath);

    // Load shader names from shaderslist.xml
    static bool LoadShaderListXml(const std::wstring& xmlPath, 
                                  std::map<std::string, std::string>& nameMap);

    // Sanitize filename (remove invalid characters)
    static std::string SanitizeFilename(const std::string& name);

    // Get filename without extension
    static std::string GetFilenameWithoutExt(const std::string& filename);
};
