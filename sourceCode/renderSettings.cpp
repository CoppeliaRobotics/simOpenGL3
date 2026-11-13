#include "renderSettings.h"
#include "log.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <string>
#include <unordered_map>

namespace modern
{

const char* const kConfigFileName = "simOpenGL3.ini";

namespace
{

std::string trim(const std::string& text)
{
    const std::size_t begin = text.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos)
        return std::string();
    const std::size_t end = text.find_last_not_of(" \t\r\n");
    return text.substr(begin, end - begin + 1);
}

std::string toLower(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

bool parseBool(const std::string& text, bool& value)
{
    const std::string lowered = toLower(text);
    if (lowered == "true" || lowered == "yes" || lowered == "on" || lowered == "1")
    {
        value = true;
        return true;
    }
    if (lowered == "false" || lowered == "no" || lowered == "off" || lowered == "0")
    {
        value = false;
        return true;
    }
    return false;
}

bool parseInt(const std::string& text, int& value)
{
    char* end = nullptr;
    const long parsed = std::strtol(text.c_str(), &end, 10);
    if (end == text.c_str() || *end != '\0')
        return false;
    value = int(parsed);
    return true;
}

bool parseFloat(const std::string& text, float& value)
{
    char* end = nullptr;
    const double parsed = std::strtod(text.c_str(), &end);
    if (end == text.c_str() || *end != '\0')
        return false;
    value = float(parsed);
    return true;
}

bool parseToneMapping(const std::string& text, ToneMapping& value)
{
    const std::string lowered = toLower(text);
    if (lowered == "none")
        value = ToneMapping::None;
    else if (lowered == "reinhard")
        value = ToneMapping::Reinhard;
    else if (lowered == "aces")
        value = ToneMapping::Aces;
    else
        return false;
    return true;
}

} // namespace

RenderSettings loadRenderSettings(const char* directory)
{
    RenderSettings settings;
    if (directory == nullptr || directory[0] == '\0')
        return settings;

    std::string path(directory);
    if (!path.empty() && path.back() != '/' && path.back() != '\\')
        path += '/';
    path += kConfigFileName;

    std::ifstream file(path);
    if (!file)
    {
        // Not fatal, but always worth saying: the defaults are in effect and a
        // file that was meant to override them is not being read.
        logWarning("no configuration file at \"%s\", using the built-in defaults", path.c_str());
        return settings;
    }

    std::unordered_map<std::string, std::string> values;
    std::string section;
    std::string line;
    int lineNumber = 0;
    while (std::getline(file, line))
    {
        ++lineNumber;

        const std::size_t comment = line.find_first_of("#;");
        if (comment != std::string::npos)
            line.erase(comment);

        line = trim(line);
        if (line.empty())
            continue;

        if (line.front() == '[' && line.back() == ']')
        {
            section = toLower(trim(line.substr(1, line.size() - 2)));
            continue;
        }

        const std::size_t separator = line.find('=');
        if (separator == std::string::npos)
        {
            logWarning("%s:%d: expected \"key = value\", line ignored", path.c_str(), lineNumber);
            continue;
        }

        const std::string key = toLower(trim(line.substr(0, separator)));
        const std::string value = trim(line.substr(separator + 1));
        values[section.empty() ? key : section + "." + key] = value;
    }

    auto reportBadValue = [&](const char* name, const std::string& value) {
        logWarning("%s: value \"%s\" for \"%s\" is not valid, keeping the default", path.c_str(),
                   value.c_str(), name);
    };

    auto takeBool = [&](const char* name, bool& target) {
        const auto it = values.find(name);
        if (it == values.end())
            return;
        if (!parseBool(it->second, target))
            reportBadValue(name, it->second);
        values.erase(it);
    };

    auto takeInt = [&](const char* name, int& target) {
        const auto it = values.find(name);
        if (it == values.end())
            return;
        if (!parseInt(it->second, target))
            reportBadValue(name, it->second);
        values.erase(it);
    };

    auto takeFloat = [&](const char* name, float& target) {
        const auto it = values.find(name);
        if (it == values.end())
            return;
        if (!parseFloat(it->second, target))
            reportBadValue(name, it->second);
        values.erase(it);
    };

    auto takeToneMapping = [&](const char* name, ToneMapping& target) {
        const auto it = values.find(name);
        if (it == values.end())
            return;
        if (!parseToneMapping(it->second, target))
            reportBadValue(name, it->second);
        values.erase(it);
    };

    takeInt("antialiasing.samples", settings.msaaSamples);

    takeBool("shadows.enabled", settings.shadowsEnabled);
    takeInt("shadows.mapsize", settings.shadowMapSize);
    takeInt("shadows.maxlayers", settings.maxShadowLayers);
    takeBool("shadows.polygonoffset", settings.polygonOffsetShadows);
    takeInt("shadows.pcfradius", settings.pcfKernelRadius);

    takeBool("shading.gammacorrect", settings.gammaCorrect);
    takeToneMapping("shading.tonemapping", settings.toneMapping);
    takeFloat("shading.exposure", settings.exposure);
    takeBool("shading.fog", settings.fogEnabled);


    takeBool("geometry.cullbackfaces", settings.cullBackFaces);
    takeBool("geometry.depthprepass", settings.depthPrepass);
    takeBool("geometry.frustumculling", settings.frustumCulling);



    // Anything left over is a key this build does not know: a typo, or a
    // setting from a newer version.  Both are worth saying out loud, because
    // the value just silently does nothing otherwise.
    for (const auto& entry : values)
        logWarning("%s: unknown setting \"%s\"", path.c_str(), entry.first.c_str());

    settings.msaaSamples = std::max(1, std::min(settings.msaaSamples, 64));
    settings.shadowMapSize = std::max(64, std::min(settings.shadowMapSize, 8192));
    settings.maxShadowLayers = std::max(1, std::min(settings.maxShadowLayers, 1024));
    settings.pcfKernelRadius = std::max(1, std::min(settings.pcfKernelRadius, 4));
    settings.exposure = std::max(0.01f, std::min(settings.exposure, 16.0f));

    return settings;
}

} // namespace modern
