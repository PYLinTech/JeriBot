#include "StoreManager.h"
#include "Config/ConfigManager.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>

#include <chrono>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <utility>
#include <vector>

#include <miniz.h>

namespace JeriBot {

namespace {

static std::string u8toStr(const std::filesystem::path& p)
{
    auto u8 = p.u8string();
    return std::string(u8.begin(), u8.end());
}

std::string md5File(const std::string& filePath)
{
    std::ifstream ifs(std::filesystem::u8path(filePath), std::ios::binary);
    if (!ifs) return {};

    unsigned char buf[64];

    unsigned int state[4] = {0x67452301u, 0xefcdab89u, 0x98badcfeu, 0x10325476u};
    unsigned char lengthBytes[8];
    uint64_t totalBytes = 0;

    auto transform = [](unsigned int* state, const unsigned char block[64]) {
        unsigned int a = state[0], b = state[1], c = state[2], d = state[3];
        unsigned int x[16];
        for (int i = 0; i < 16; ++i)
            x[i] = static_cast<unsigned int>(block[i * 4])
                  | (static_cast<unsigned int>(block[i * 4 + 1]) << 8)
                  | (static_cast<unsigned int>(block[i * 4 + 2]) << 16)
                  | (static_cast<unsigned int>(block[i * 4 + 3]) << 24);

        static const unsigned int T[64] = {
            0xd76aa478,0xe8c7b756,0x242070db,0xc1bdceee,0xf57c0faf,0x4787c62a,0xa8304613,0xfd469501,
            0x698098d8,0x8b44f7af,0xffff5bb1,0x895cd7be,0x6b901122,0xfd987193,0xa679438e,0x49b40821,
            0xf61e2562,0xc040b340,0x265e5a51,0xe9b6c7aa,0xd62f105d,0x02441453,0xd8a1e681,0xe7d3fbc8,
            0x21e1cde6,0xc33707d6,0xf4d50d87,0x455a14ed,0xa9e3e905,0xfcefa3f8,0x676f02d9,0x8d2a4c8a,
            0xfffa3942,0x8771f681,0x6d9d6122,0xfde5380c,0xa4beea44,0x4bdecfa9,0xf6bb4b60,0xbebfbc70,
            0x289b7ec6,0xeaa127fa,0xd4ef3085,0x04881d05,0xd9d4d039,0xe6db99e5,0x1fa27cf8,0xc4ac5665,
            0xf4292244,0x432aff97,0xab9423a7,0xfc93a039,0x655b59c3,0x8f0ccc92,0xffeff47d,0x85845dd1,
            0x6fa87e4f,0xfe2ce6e0,0xa3014314,0x4e0811a1,0xf7537e82,0xbd3af235,0x2ad7d2bb,0xeb86d391
        };
        static const int s[64] = {
            7,12,17,22,7,12,17,22,7,12,17,22,7,12,17,22,
            5,9,14,20,5,9,14,20,5,9,14,20,5,9,14,20,
            4,11,16,23,4,11,16,23,4,11,16,23,4,11,16,23,
            6,10,15,21,6,10,15,21,6,10,15,21,6,10,15,21
        };

        for (int i = 0; i < 64; ++i) {
            unsigned int f, g;
            if (i < 16)      { f = (b & c) | ((~b) & d); g = static_cast<unsigned int>(i); }
            else if (i < 32) { f = (d & b) | ((~d) & c); g = static_cast<unsigned int>((5 * i + 1) % 16); }
            else if (i < 48) { f = b ^ c ^ d;            g = static_cast<unsigned int>((3 * i + 5) % 16); }
            else              { f = c ^ (b | (~d));        g = static_cast<unsigned int>((7 * i) % 16); }
            f = f + a + T[i] + x[g];
            a = d; d = c; c = b;
            b = b + ((f << s[i]) | (f >> (32 - s[i])));
        }
        state[0] += a; state[1] += b; state[2] += c; state[3] += d;
    };

    while (true) {
        ifs.read(reinterpret_cast<char*>(buf), 64);
        std::streamsize bytesRead = ifs.gcount();
        totalBytes += static_cast<uint64_t>(bytesRead);
        if (bytesRead == 64) {
            transform(state, buf);
        } else {
            std::size_t len = static_cast<std::size_t>(bytesRead);
            unsigned char block[64] = {};
            std::memcpy(block, buf, len);
            block[len] = 0x80;
            if (len >= 56) {
                transform(state, block);
                std::memset(block, 0, 64);
            }
            for (int i = 0; i < 8; ++i)
                lengthBytes[i] = static_cast<unsigned char>((totalBytes * 8) >> (i * 8));
            std::memcpy(block + 56, lengthBytes, 8);
            transform(state, block);
            break;
        }
    }

    unsigned char digest[16];
    for (int i = 0; i < 4; ++i) {
        digest[i * 4 + 0] = static_cast<unsigned char>(state[i]);
        digest[i * 4 + 1] = static_cast<unsigned char>(state[i] >> 8);
        digest[i * 4 + 2] = static_cast<unsigned char>(state[i] >> 16);
        digest[i * 4 + 3] = static_cast<unsigned char>(state[i] >> 24);
    }

    char hex[33];
    for (int i = 0; i < 16; ++i)
        snprintf(hex + i * 2, 3, "%02x", digest[i]);
    return {hex, 32};
}

static const char* typeToPlural(const std::string& type)
{
    if (type == "personality") return "Personalities";
    if (type == "skill")       return "Skills";
    if (type == "memory")      return "Memories";
    if (type == "tool")        return "Tools";
    return nullptr;
}

bool isValidType(const std::string& type)
{
    return type == "personality" || type == "skill"
        || type == "memory"      || type == "tool";
}

bool isValidSourceName(const std::string& source)
{
    return source == "china" || source == "global" || source == "custom";
}

std::string normalizeBaseUrl(std::string url)
{
    while (!url.empty() && (url.back() == ' ' || url.back() == '\t' || url.back() == '\r' || url.back() == '\n')) {
        url.pop_back();
    }
    size_t start = 0;
    while (start < url.size() && (url[start] == ' ' || url[start] == '\t' || url[start] == '\r' || url[start] == '\n')) {
        ++start;
    }
    if (start > 0) url.erase(0, start);
    if (url.rfind("env:", 0) != 0 && !url.empty() && url.back() != '/') url.push_back('/');
    return url;
}

std::string trimString(std::string value)
{
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t' || value.back() == '\r' || value.back() == '\n')) {
        value.pop_back();
    }
    size_t start = 0;
    while (start < value.size() && (value[start] == ' ' || value[start] == '\t' || value[start] == '\r' || value[start] == '\n')) {
        ++start;
    }
    if (start > 0) value.erase(0, start);
    return value;
}

bool isEnvReference(const std::string& value)
{
    return value.rfind("env:", 0) == 0 && value.size() > 4;
}

bool isHttpAbsoluteUrl(const std::string& value)
{
    return value.rfind("http://", 0) == 0 || value.rfind("https://", 0) == 0;
}

std::string toLowerAscii(std::string value)
{
    for (char& c : value) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return value;
}

bool isSafeIdPart(const std::string& value)
{
    if (value.empty() || value.size() > 128) return false;
    for (unsigned char c : value) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.') continue;
        return false;
    }
    return value.find("..") == std::string::npos;
}

bool isSafeTempId(const std::string& value)
{
    if (value.size() < 24 || value.size() > 80) return false;
    for (unsigned char c : value) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.') continue;
        return false;
    }
    return value.rfind("local-", 0) == 0 && value.find("..") == std::string::npos;
}

bool readFileToBuffer(const std::string& path, std::vector<unsigned char>& buffer)
{
    std::ifstream ifs(std::filesystem::u8path(path), std::ios::binary | std::ios::ate);
    if (!ifs) return false;
    auto size = ifs.tellg();
    if (size < 0) return false;
    ifs.seekg(0, std::ios::beg);
    buffer.resize(static_cast<size_t>(size));
    if (size > 0 && !ifs.read(reinterpret_cast<char*>(buffer.data()), size)) return false;
    return true;
}

bool writeFileFromBuffer(const std::string& path, const void* data, size_t size)
{
    std::ofstream ofs(std::filesystem::u8path(path), std::ios::binary);
    if (!ofs) return false;
    ofs.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
    return ofs.good();
}

bool extractZipEntryToBuffer(const void* data, size_t size,
                             const char* entryName,
                             bool ignorePath,
                             std::vector<unsigned char>& outBuffer)
{
    mz_zip_archive zip = {};
    if (!mz_zip_reader_init_mem(&zip, data, size, 0)) return false;

    int fileIndex = mz_zip_reader_locate_file(&zip, entryName, nullptr, ignorePath ? MZ_ZIP_FLAG_IGNORE_PATH : 0);
    if (fileIndex < 0) {
        mz_zip_reader_end(&zip);
        return false;
    }

    size_t uncompSize = 0;
    void* pData = mz_zip_reader_extract_to_heap(&zip, static_cast<mz_uint>(fileIndex), &uncompSize, 0);
    if (!pData) {
        mz_zip_reader_end(&zip);
        return false;
    }

    outBuffer.assign(static_cast<unsigned char*>(pData), static_cast<unsigned char*>(pData) + uncompSize);
    mz_free(pData);
    mz_zip_reader_end(&zip);
    return true;
}

std::string makeInstallTempName(const std::string& id)
{
    auto ticks = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    return "." + id + ".installing-" + std::to_string(ticks);
}

bool isPathInside(const std::filesystem::path& child, const std::filesystem::path& parent)
{
    auto childNorm = child.lexically_normal();
    auto parentNorm = parent.lexically_normal();
    auto childIt = childNorm.begin();
    auto parentIt = parentNorm.begin();
    for (; parentIt != parentNorm.end(); ++parentIt, ++childIt) {
        if (childIt == childNorm.end() || *childIt != *parentIt) return false;
    }
    return true;
}

std::string makeLocalTempId()
{
    auto ticks = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    return "local-" + std::to_string(ticks) + ".zip";
}

std::string joinSourceRelativeUrl(std::string baseUrl, std::string relativePath)
{
    baseUrl = normalizeBaseUrl(baseUrl);
    relativePath = trimString(relativePath);
    while (!relativePath.empty() && relativePath.front() == '/') {
        relativePath.erase(relativePath.begin());
    }
    return baseUrl + relativePath;
}

std::wstring utf8ToWide(const std::string& value)
{
    int len = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, nullptr, 0);
    if (len <= 0) return {};
    std::wstring wide(static_cast<size_t>(len), L'\0');
    if (MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, wide.data(), len) <= 0) return {};
    if (!wide.empty() && wide.back() == L'\0') wide.pop_back();
    return wide;
}

std::string wideToUtf8(const wchar_t* value, DWORD len)
{
    if (!value || len == 0) return {};
    int u8Len = WideCharToMultiByte(CP_UTF8, 0, value, static_cast<int>(len), nullptr, 0, nullptr, nullptr);
    if (u8Len <= 0) return {};
    std::string utf8(static_cast<size_t>(u8Len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value, static_cast<int>(len), utf8.data(), u8Len, nullptr, nullptr);
    return utf8;
}

} // namespace

bool StoreManager::initialize(ConfigManager* configMgr, std::string& error)
{
    configMgr_ = configMgr;
    configDir_ = configMgr->configDir();

    const Json& cfg = configMgr->config();
    if (!cfg.isObject() || !cfg.contains("Store") || !cfg["Store"].isObject()) {
        error = "配置中缺少 Store 段";
        return false;
    }

    const Json& store = cfg["Store"];
    if (!store.contains("Source") || !store["Source"].isString()
        || !isValidSourceName(store["Source"].asString())) {
        error = "配置中缺少 Store.Source";
        return false;
    }
    if (!store.contains("Sources") || !store["Sources"].isObject()) {
        error = "配置中缺少 Store.Sources";
        return false;
    }
    const Json& sources = store["Sources"];
    const char* fixedSources[] = {"china", "global", "custom"};
    for (const char* key : fixedSources) {
        if (!sources.contains(key) || !sources[key].isString()) {
            error = "配置中缺少 Store.Sources." + std::string(key);
            return false;
        }
    }

    return true;
}

std::string StoreManager::typeToDirName(const std::string& type) const
{
    const char* plural = typeToPlural(type);
    return plural ? plural : type;
}

std::string StoreManager::typeDir(const std::string& type) const
{
    std::filesystem::path base = std::filesystem::u8path(configDir_);
    return u8toStr(base / std::filesystem::u8path(typeToDirName(type)));
}

std::string StoreManager::loadJsonPath(const std::string& type) const
{
    std::filesystem::path base = std::filesystem::u8path(configDir_);
    return u8toStr(base / std::filesystem::u8path(typeToDirName(type)) / L"Load.json");
}

std::string StoreManager::packageDir(const std::string& type,
                                      const std::string& uploader,
                                      const std::string& id) const
{
    std::filesystem::path base = std::filesystem::u8path(configDir_);
    return u8toStr(base / std::filesystem::u8path(typeToDirName(type))
               / std::filesystem::u8path(uploader)
               / std::filesystem::u8path(id));
}

std::string StoreManager::getSourceUrl() const
{
    const Json& store = configMgr_->config()["Store"];
    const std::string& sourceName = store["Source"].asString();
    std::string value = getSourceRawUrl(sourceName);
    if (isEnvReference(value)) {
        std::string envName = value.substr(4);
        std::wstring wEnv = utf8ToWide(envName);
        if (wEnv.empty()) return {};
        wchar_t buffer[32767];
        DWORD len = GetEnvironmentVariableW(wEnv.c_str(), buffer, 32767);
        if (len == 0) return {};
        std::string resolved = wideToUtf8(buffer, len);
        return normalizeBaseUrl(resolved);
    }
    return normalizeBaseUrl(value);
}

std::string StoreManager::getSourceRawUrl(const std::string& source) const
{
    const Json& sources = configMgr_->config()["Store"]["Sources"];
    if (!sources.isObject() || !sources.contains(source) || !sources[source].isString()) return {};
    return sources[source].asString();
}

std::string StoreManager::getCurrentSource() const
{
    return configMgr_->config()["Store"]["Source"].asString();
}

Json StoreManager::getSourcesList() const
{
    Json result = Json::Array{};
    const Json& sources = configMgr_->config()["Store"]["Sources"];
    if (sources.isObject()) {
        const char* fixedSources[] = {"china", "global", "custom"};
        for (const char* key : fixedSources) {
            Json entry = Json::Object{};
            entry["name"] = key;
            if (sources.contains(key) && sources[key].isString()) {
                entry["url"] = sources[key].asString();
            } else {
                entry["url"] = "";
            }
            result.asArray().push_back(std::move(entry));
        }
    }
    return result;
}

bool StoreManager::setSource(const std::string& source, std::string& error)
{
    if (!isValidSourceName(source)) {
        error = "未知的源: " + source;
        return false;
    }

    Json& store = configMgr_->mutableConfig()["Store"];
    if (!store.contains("Sources") || !store["Sources"].isObject()) {
        error = "配置中缺少 Sources 映射";
        return false;
    }
    if (!store["Sources"].contains(source)) {
        error = "未知的源: " + source;
        return false;
    }
    store["Source"] = source;
    return configMgr_->save(error);
}

bool StoreManager::setCustomSourceUrl(const std::string& url, std::string& error)
{
    std::string raw = trimString(url);
    if (raw.empty()) {
        error = "自定义源不能为空";
        return false;
    }

    Json& store = configMgr_->mutableConfig()["Store"];
    if (!store.contains("Sources") || !store["Sources"].isObject()) {
        store["Sources"] = Json::Object{};
    }
    store["Sources"]["custom"] = raw;
    return configMgr_->save(error);
}

bool StoreManager::getInstalledIcon(const std::string& type,
                                    const std::string& id,
                                    std::vector<unsigned char>& outData,
                                    std::string& error)
{
    if (!isValidType(type)) {
        error = "无效的类型: " + type;
        return false;
    }

    Json loadDoc = loadLoadJson(type);
    if (!loadDoc.contains("installed") || !loadDoc["installed"].isObject()) {
        error = "未找到已安装的包";
        return false;
    }

    if (!isSafeIdPart(id)) {
        error = "无效的包 ID";
        return false;
    }

    std::string uploader;
    for (const auto& [key, value] : loadDoc["installed"].asObject()) {
        std::string entryId = value.contains("id") && value["id"].isString()
                              ? value["id"].asString()
                              : (key.find('.') != std::string::npos ? key.substr(key.find('.') + 1) : key);
        if (entryId != id) continue;
        if (value.contains("uploader") && value["uploader"].isString()) {
            uploader = value["uploader"].asString();
        } else if (key.find('.') != std::string::npos) {
            uploader = key.substr(0, key.find('.'));
        }
        break;
    }

    if (uploader.empty() || !isSafeIdPart(uploader)) {
        error = "未找到已安装的包: " + id;
        return false;
    }

    std::filesystem::path iconPath = std::filesystem::u8path(packageDir(type, uploader, id)) / std::filesystem::u8path("favicon.ico");
    if (!std::filesystem::exists(iconPath)) {
        error = "未找到图标";
        return false;
    }

    return readFileToBuffer(u8toStr(iconPath), outData);
}

bool StoreManager::getLocalZipIcon(const std::string& tempId,
                                   std::vector<unsigned char>& outData,
                                   std::string& error)
{
    std::string tempZipPath = tempZipPathFromId(tempId, error);
    if (tempZipPath.empty()) return false;

    std::vector<unsigned char> zipBuffer;
    if (!readFileToBuffer(tempZipPath, zipBuffer)) {
        error = "无法读取临时 ZIP 文件";
        return false;
    }
    if (!extractZipEntryToBuffer(zipBuffer.data(), zipBuffer.size(), "favicon.ico", false, outData)
        && !extractZipEntryToBuffer(zipBuffer.data(), zipBuffer.size(), "favicon.ico", true, outData)) {
        error = "ZIP 中未找到 favicon.ico";
        return false;
    }
    return true;
}

bool StoreManager::getRemoteIcon(const std::string& type,
                                 const std::string& id,
                                 std::vector<unsigned char>& outData,
                                 std::string& error)
{
    if (!isValidType(type)) {
        error = "无效的类型: " + type;
        return false;
    }
    if (!isSafeIdPart(id)) {
        error = "无效的包 ID";
        return false;
    }

    std::string baseUrl = getSourceUrl();
    if (baseUrl.empty()) {
        error = "未配置商店源";
        return false;
    }

    Json pkg;
    if (!findPackageInList(type, id, pkg, error)) return false;

    std::string iconUrl = resolvePackageIconUrl(pkg, baseUrl, error);
    if (iconUrl.empty()) return false;

    auto iconResp = http_.get(iconUrl, {}, error);
    if (!error.empty()) return false;
    if (iconResp.statusCode != 200) {
        error = "读取图标失败 HTTP " + std::to_string(iconResp.statusCode);
        return false;
    }

    outData.assign(iconResp.body.begin(), iconResp.body.end());
    return !outData.empty();
}

Json StoreManager::loadLoadJson(const std::string& type)
{
    std::string path = loadJsonPath(type);
    std::string error;
    Json data = Json::loadFile(path, error);
    if (!error.empty() || !data.isObject()) {
        data = Json::Object{};
        data["type"] = type;
        data["installed"] = Json::Object{};
    }
    return data;
}

bool StoreManager::saveLoadJson(const std::string& type, const Json& data)
{
    std::filesystem::path dir = std::filesystem::u8path(typeDir(type));
    std::error_code ec;
    if (!std::filesystem::exists(dir, ec)) {
        if (!std::filesystem::create_directories(dir, ec)) return false;
    }
    std::string path = loadJsonPath(type);
    std::ofstream ofs(std::filesystem::u8path(path), std::ios::binary);
    if (!ofs) return false;
    std::string content = data.dump(2);
    ofs.write(content.c_str(), static_cast<std::streamsize>(content.size()));
    return ofs.good();
}

std::string StoreManager::fetchList(const std::string& type, std::string& error)
{
    Json listDoc;
    std::string body;
    if (!fetchListDocument(type, listDoc, &body, error)) return {};
    return body;
}

bool StoreManager::fetchListDocument(const std::string& type,
                                     Json& outList,
                                     std::string* outBody,
                                     std::string& error)
{
    if (!isValidType(type)) {
        error = "无效的类型: " + type;
        return false;
    }

    std::string baseUrl = getSourceUrl();
    if (baseUrl.empty()) {
        error = "未配置商店源";
        return false;
    }

    std::string listUrl = baseUrl + typeToDirName(type) + "/List.json";
    auto listResp = http_.get(listUrl, {}, error);
    if (!error.empty()) return false;
    if (listResp.statusCode != 200) {
        error = "拉取列表失败 HTTP " + std::to_string(listResp.statusCode);
        return false;
    }

    Json listDoc = Json::parse(listResp.body, error);
    if (!error.empty() || !listDoc.isObject()) {
        error = "列表 JSON 解析失败";
        return false;
    }

    if (!listDoc.contains("packages") || !listDoc["packages"].isArray()) {
        error = "列表 JSON 缺少 packages";
        return false;
    }

    if (outBody) *outBody = listResp.body;
    outList = std::move(listDoc);
    return true;
}

bool StoreManager::findPackageInList(const std::string& type,
                                     const std::string& id,
                                     Json& outPackage,
                                     std::string& error)
{
    Json listDoc;
    if (!fetchListDocument(type, listDoc, nullptr, error)) return false;

    const Json& packages = listDoc["packages"];
    for (size_t i = 0; i < packages.size(); ++i) {
        const Json& pkg = packages[i];
        if (pkg.contains("id") && pkg["id"].isString() && pkg["id"].asString() == id) {
            outPackage = pkg;
            return true;
        }
    }

    error = "未找到包: " + id;
    return false;
}

std::string StoreManager::resolvePackageZipUrl(const Json& package,
                                               const std::string& baseUrl,
                                               std::string& error) const
{
    if (!package.contains("zip") || !package["zip"].isString()) {
        error = "包缺少 zip 字段";
        return {};
    }

    std::string zip = trimString(package["zip"].asString());
    if (zip.empty()) {
        error = "zip 字段不能为空";
        return {};
    }
    if (zip.find('\\') != std::string::npos) {
        error = "zip 路径不能包含反斜杠";
        return {};
    }
    if (isHttpAbsoluteUrl(zip)) {
        return zip;
    }
    if (zip.rfind("//", 0) == 0 || zip.find("..") != std::string::npos) {
        error = "zip 路径无效";
        return {};
    }
    return joinSourceRelativeUrl(baseUrl, zip);
}

std::string StoreManager::resolvePackageIconUrl(const Json& package,
                                                const std::string& baseUrl,
                                                std::string& error) const
{
    if (!package.contains("icon") || !package["icon"].isString()) {
        error = "包缺少 icon 字段";
        return {};
    }

    std::string icon = trimString(package["icon"].asString());
    if (icon.empty()) {
        error = "icon 字段不能为空";
        return {};
    }
    if (icon.rfind("data:image/", 0) == 0) {
        error = "内嵌图标不需要远程读取";
        return {};
    }
    if (icon.find('\\') != std::string::npos) {
        error = "icon 路径不能包含反斜杠";
        return {};
    }
    if (isHttpAbsoluteUrl(icon)) {
        return icon;
    }
    if (icon.rfind("//", 0) == 0 || icon.find("..") != std::string::npos) {
        error = "icon 路径无效";
        return {};
    }
    return joinSourceRelativeUrl(baseUrl, icon);
}

bool StoreManager::installFromRemote(const std::string& type,
                                      const std::string& id,
                                      std::string& error)
{
    if (!isValidType(type)) {
        error = "无效的类型: " + type;
        return false;
    }
    if (!isSafeIdPart(id)) {
        error = "无效的包 ID";
        return false;
    }

    std::string baseUrl = getSourceUrl();
    if (baseUrl.empty()) {
        error = "未配置商店源";
        return false;
    }

    Json pkg;
    if (!findPackageInList(type, id, pkg, error)) return false;

    std::string uploader;
    if (pkg.contains("uploader") && pkg["uploader"].isString())
        uploader = pkg["uploader"].asString();
    if (uploader.empty() || !isSafeIdPart(uploader)) {
        error = "包缺少 uploader 字段";
        return false;
    }

    std::string expectedMd5;
    if (pkg.contains("md5") && pkg["md5"].isString())
        expectedMd5 = trimString(pkg["md5"].asString());

    std::string zipUrl = resolvePackageZipUrl(pkg, baseUrl, error);
    if (zipUrl.empty()) return false;

    std::string jeriTempDirStr = ensureJeriTempDir(error);
    if (jeriTempDirStr.empty()) return false;

    std::string zipFileName = type + "-" + id + ".zip";
    std::filesystem::path tempZipPath = std::filesystem::u8path(jeriTempDirStr)
                                        / std::filesystem::u8path(zipFileName);
    std::string tempZipPathStr = u8toStr(tempZipPath);

    auto zipResp = http_.get(zipUrl, {}, error);
    if (!error.empty()) return false;
    if (zipResp.statusCode != 200) {
        error = "下载 ZIP 失败 HTTP " + std::to_string(zipResp.statusCode);
        return false;
    }

    if (!writeFileFromBuffer(tempZipPathStr, zipResp.body.data(), zipResp.body.size())) {
        error = "无法写入临时 ZIP 文件";
        return false;
    }

    std::error_code ec;
    if (!expectedMd5.empty()) {
        std::string actualMd5 = computeMd5(tempZipPathStr);
        if (toLowerAscii(actualMd5) != toLowerAscii(expectedMd5)) {
            std::filesystem::remove(tempZipPath, ec);
            error = "MD5 校验失败: 期望 " + expectedMd5 + " 实际 " + actualMd5;
            return false;
        }
    }

    Json manifest = extractManifestFromMemory(zipResp.body.data(), zipResp.body.size(), error);
    if (!error.empty()) {
        std::filesystem::remove(tempZipPath, ec);
        return false;
    }
    if (!manifest.contains("id") || !manifest["id"].isString() || manifest["id"].asString() != id) {
        error = "manifest.json 的 ID 与包列表不一致";
        std::filesystem::remove(tempZipPath, ec);
        return false;
    }
    if (!manifest.contains("type") || !manifest["type"].isString() || manifest["type"].asString() != type) {
        error = "manifest.json 的类型与当前分类不一致";
        std::filesystem::remove(tempZipPath, ec);
        return false;
    }
    if (!manifest.contains("uploader") || !manifest["uploader"].isString() || manifest["uploader"].asString() != uploader) {
        error = "manifest.json 的上传者与包列表不一致";
        std::filesystem::remove(tempZipPath, ec);
        return false;
    }

    if (!installZipToPackage(tempZipPathStr, type, id, uploader, manifest, error)) {
        std::filesystem::remove(tempZipPath, ec);
        return false;
    }

    std::filesystem::remove(tempZipPath, ec);
    return true;
}


bool StoreManager::installFromLocal(const std::string& type,
                                      const std::string& id,
                                      const std::string& tempId,
                                      std::string& error)
{
    if (!isValidType(type)) {
        error = "无效的类型: " + type;
        return false;
    }
    if (!isSafeIdPart(id)) {
        error = "无效的包 ID";
        return false;
    }

    std::string tempZipPath = tempZipPathFromId(tempId, error);
    if (tempZipPath.empty()) return false;

    std::filesystem::path zipPath = std::filesystem::u8path(tempZipPath);
    if (!std::filesystem::exists(zipPath)) {
        error = "临时 ZIP 文件不存在";
        return false;
    }

    std::vector<unsigned char> zipBuffer;
    if (!readFileToBuffer(tempZipPath, zipBuffer)) {
        error = "无法读取临时 ZIP 文件";
        return false;
    }

    Json manifest = extractManifestFromMemory(zipBuffer.data(), zipBuffer.size(), error);
    if (!error.empty()) return false;

    if (!manifest.contains("id") || !manifest["id"].isString() || manifest["id"].asString() != id) {
        error = "安装包 ID 与预览结果不一致";
        return false;
    }
    if (!manifest.contains("type") || !manifest["type"].isString() || manifest["type"].asString() != type) {
        error = "安装包类型与预览结果不一致";
        return false;
    }

    std::string uploader;
    if (manifest.contains("uploader") && manifest["uploader"].isString())
        uploader = manifest["uploader"].asString();
    if (uploader.empty() || !isSafeIdPart(uploader)) {
        error = "manifest.json 缺少 uploader";
        return false;
    }

    if (!installZipToPackage(tempZipPath, type, id, uploader, manifest, error)) {
        return false;
    }

    std::error_code ec;
    std::filesystem::remove(zipPath, ec);
    return true;
}

bool StoreManager::uninstall(const std::string& type,
                              const std::string& id,
                              std::string& error)
{
    if (!isValidType(type)) {
        error = "无效的类型: " + type;
        return false;
    }
    if (!isSafeIdPart(id)) {
        error = "无效的包 ID";
        return false;
    }

    Json loadDoc = loadLoadJson(type);
    if (!loadDoc.contains("installed") || !loadDoc["installed"].isObject()) {
        error = "未找到已安装的包";
        return false;
    }

    Json& installed = loadDoc["installed"];
    std::string key;
    bool found = false;
    if (installed.isObject()) {
        for (const auto& [k, v] : installed.asObject()) {
            std::string entryId = v.contains("id") && v["id"].isString()
                                  ? v["id"].asString()
                                  : (k.find('.') != std::string::npos ? k.substr(k.find('.') + 1) : k);
            if (entryId == id) {
                key = k;
                found = true;
                break;
            }
        }
    }

    if (!found) {
        error = "未找到已安装的包: " + id;
        return false;
    }

    const Json& entry = installed[key];
    std::string uploader;
    if (entry.contains("uploader") && entry["uploader"].isString())
        uploader = entry["uploader"].asString();

    if (!uploader.empty()) {
        std::filesystem::path pkgDir = std::filesystem::u8path(
            packageDir(type, uploader, id));
        std::error_code ec;
        if (std::filesystem::exists(pkgDir, ec)) {
            std::filesystem::remove_all(pkgDir, ec);
        }
    }

    installed.asObject().erase(key);
    if (!saveLoadJson(type, loadDoc)) {
        error = "无法更新 Load.json";
        return false;
    }

    return true;
}

std::string StoreManager::getInstalled(const std::string& type, std::string& error)
{
    if (!isValidType(type)) {
        error = "无效的类型: " + type;
        return {};
    }
    Json loadDoc = loadLoadJson(type);
    return loadDoc.dump(2);
}

std::string StoreManager::computeMd5(const std::string& filePath)
{
    return md5File(filePath);
}

std::string StoreManager::ensureJeriTempDir(std::string& error)
{
    wchar_t tempDir[MAX_PATH];
    GetTempPathW(MAX_PATH, tempDir);
    std::filesystem::path tempBase = tempDir;
    auto jeriTempDir = tempBase / L"JeriBot";
    std::error_code ec;
    if (!std::filesystem::exists(jeriTempDir, ec)) {
        if (!std::filesystem::create_directories(jeriTempDir, ec)) {
            error = "无法创建临时目录";
            return {};
        }
    }
    return u8toStr(jeriTempDir);
}

Json StoreManager::extractManifestFromMemory(const void* data, size_t size, std::string& error)
{
    mz_zip_archive zip = {};
    if (!mz_zip_reader_init_mem(&zip, data, size, 0)) {
        error = "无法打开 ZIP 数据";
        return {};
    }

    int fileIndex = mz_zip_reader_locate_file(&zip, "manifest.json", nullptr, 0);
    if (fileIndex < 0) {
        fileIndex = mz_zip_reader_locate_file(&zip, "manifest.json", nullptr, MZ_ZIP_FLAG_IGNORE_PATH);
    }
    if (fileIndex < 0) {
        mz_zip_reader_end(&zip);
        error = "ZIP 中未找到 manifest.json";
        return {};
    }

    size_t uncompSize = 0;
    void* pData = mz_zip_reader_extract_to_heap(&zip, static_cast<mz_uint>(fileIndex), &uncompSize, 0);
    if (!pData) {
        mz_zip_reader_end(&zip);
        error = "解压 manifest.json 失败";
        return {};
    }

    std::string manifestStr(static_cast<const char*>(pData), uncompSize);
    mz_free(pData);
    mz_zip_reader_end(&zip);

    std::string parseErr;
    Json manifest = Json::parse(manifestStr, parseErr);
    if (!parseErr.empty() || !manifest.isObject()) {
        error = "manifest.json 解析失败";
        return {};
    }
    return manifest;
}

Json StoreManager::buildLoadEntry(const Json& manifest, const std::string& id)
{
    Json entry = Json::Object{};
    entry["id"] = id;
    entry["status"] = "enabled";
    entry["uploader"] = manifest.contains("uploader") && manifest["uploader"].isString()
                         ? manifest["uploader"].asString() : std::string();
    entry["name"] = manifest.contains("name") && manifest["name"].isString()
                     ? manifest["name"].asString() : id;
    entry["description"] = manifest.contains("description") && manifest["description"].isString()
                            ? manifest["description"].asString() : std::string();
    entry["version"] = manifest.contains("version") && manifest["version"].isString()
                        ? manifest["version"].asString() : "0.0.0";
    entry["update_time"] = manifest.contains("update_time") && manifest["update_time"].isNumber()
                            ? manifest["update_time"].asNumber() : 0.0;
    entry["install_time"] = static_cast<double>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());

    if (manifest.contains("load") && manifest["load"].isArray()) {
        Json files = Json::Array{};
        const Json::Array& arr = manifest["load"].asArray();
        for (size_t i = 0; i < arr.size(); ++i) {
            if (arr[i].isString()) files.asArray().push_back(arr[i].asString());
        }
        entry["files"] = std::move(files);
    } else {
        entry["files"] = Json::Array{};
    }
    if (manifest.contains("icon") && manifest["icon"].isString()) {
        entry["icon"] = manifest["icon"].asString();
    }
    return entry;
}

std::string StoreManager::previewLocalZipData(const std::string& zipData,
                                                std::string& outTempId,
                                                std::string& error)
{
    std::string jeriTempDirStr = ensureJeriTempDir(error);
    if (jeriTempDirStr.empty()) return {};

    Json manifest = extractManifestFromMemory(zipData.data(), zipData.size(), error);
    if (!error.empty()) return {};

    if (!manifest.contains("type") || !manifest["type"].isString() || !isValidType(manifest["type"].asString())) {
        error = "manifest.json 缺少有效类型";
        return {};
    }
    if (!manifest.contains("id") || !manifest["id"].isString() || !isSafeIdPart(manifest["id"].asString())) {
        error = "manifest.json 缺少有效 ID";
        return {};
    }
    if (!manifest.contains("uploader") || !manifest["uploader"].isString() || !isSafeIdPart(manifest["uploader"].asString())) {
        error = "manifest.json 缺少有效上传者";
        return {};
    }

    std::string type = manifest["type"].asString();
    std::string id = manifest["id"].asString();

    std::string tempId = makeLocalTempId();
    std::filesystem::path tempZipPath = std::filesystem::u8path(jeriTempDirStr)
                                        / std::filesystem::u8path(tempId);

    if (!writeFileFromBuffer(u8toStr(tempZipPath), zipData.data(), zipData.size())) {
        error = "无法写入临时 ZIP 文件";
        return {};
    }
    outTempId = tempId;

    Json result = Json::Object{};
    result["result"] = "success";
    result["temp_id"] = tempId;
    result["type"] = type;
    result["id"] = id;
    result["uploader"] = manifest["uploader"].asString();
    if (manifest.contains("name") && manifest["name"].isString())
        result["name"] = manifest["name"].asString();
    if (manifest.contains("description") && manifest["description"].isString())
        result["description"] = manifest["description"].asString();
    if (manifest.contains("version") && manifest["version"].isString())
        result["version"] = manifest["version"].asString();
    if (manifest.contains("icon") && manifest["icon"].isString())
        result["icon"] = manifest["icon"].asString();

    return result.dump(2);
}

std::string StoreManager::tempZipPathFromId(const std::string& tempId, std::string& error) const
{
    if (!isSafeTempId(tempId) || tempId.find('/') != std::string::npos || tempId.find('\\') != std::string::npos) {
        error = "无效的临时安装包";
        return {};
    }

    wchar_t tempDir[MAX_PATH];
    GetTempPathW(MAX_PATH, tempDir);
    std::filesystem::path jeriTempDir = std::filesystem::path(tempDir) / L"JeriBot";
    std::filesystem::path zipPath = jeriTempDir / std::filesystem::u8path(tempId);
    if (!isPathInside(zipPath, jeriTempDir)) {
        error = "无效的临时安装包";
        return {};
    }
    return u8toStr(zipPath);
}

bool StoreManager::installZipToPackage(const std::string& zipPath,
                                       const std::string& type,
                                       const std::string& id,
                                       const std::string& uploader,
                                       const Json& manifest,
                                       std::string& error)
{
    std::filesystem::path destDir = std::filesystem::u8path(packageDir(type, uploader, id));
    std::filesystem::path parentDir = destDir.parent_path();
    std::filesystem::path tempDest = parentDir / std::filesystem::u8path(makeInstallTempName(id));
    std::error_code ec;

    std::filesystem::create_directories(parentDir, ec);
    if (ec) {
        error = "无法创建包目录";
        return false;
    }
    std::filesystem::remove_all(tempDest, ec);
    if (!std::filesystem::create_directories(tempDest, ec)) {
        error = "无法创建临时安装目录";
        return false;
    }

    if (!extractZip(zipPath, u8toStr(tempDest), error)) {
        std::filesystem::remove_all(tempDest, ec);
        return false;
    }

    if (!std::filesystem::exists(tempDest / L"manifest.json", ec)) {
        error = "ZIP 中缺少 manifest.json";
        std::filesystem::remove_all(tempDest, ec);
        return false;
    }

    Json loadDoc = loadLoadJson(type);
    std::string key = uploader + "." + id;
    Json entry = buildLoadEntry(manifest, id);

    std::filesystem::path backupDir;
    if (std::filesystem::exists(destDir, ec)) {
        backupDir = parentDir / std::filesystem::u8path("." + id + ".backup-" + std::to_string(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count()));
        std::filesystem::rename(destDir, backupDir, ec);
        if (ec) {
            error = "无法替换已有包目录";
            std::filesystem::remove_all(tempDest, ec);
            return false;
        }
    }

    std::filesystem::rename(tempDest, destDir, ec);
    if (ec) {
        if (!backupDir.empty()) {
            std::error_code restoreEc;
            std::filesystem::rename(backupDir, destDir, restoreEc);
        }
        error = "无法完成安装";
        std::filesystem::remove_all(tempDest, ec);
        return false;
    }

    loadDoc["installed"].asObject()[key] = std::move(entry);
    if (!saveLoadJson(type, loadDoc)) {
        std::filesystem::remove_all(destDir, ec);
        if (!backupDir.empty()) {
            std::error_code restoreEc;
            std::filesystem::rename(backupDir, destDir, restoreEc);
        }
        error = "无法更新 Load.json";
        return false;
    }

    if (!backupDir.empty()) {
        std::filesystem::remove_all(backupDir, ec);
    }
    return true;
}

bool StoreManager::extractZip(const std::string& zipPath,
                               const std::string& destDir,
                               std::string& error)
{
    std::vector<unsigned char> zipBuffer;
    if (!readFileToBuffer(zipPath, zipBuffer)) {
        error = "无法读取 ZIP 文件";
        return false;
    }

    mz_zip_archive zip = {};
    if (!mz_zip_reader_init_mem(&zip, zipBuffer.data(), zipBuffer.size(), 0)) {
        error = "无法打开 ZIP 文件";
        return false;
    }

    std::filesystem::path dest = std::filesystem::u8path(destDir);
    mz_uint numFiles = mz_zip_reader_get_num_files(&zip);
    if (numFiles > 4096) {
        error = "ZIP 文件数量过多";
        mz_zip_reader_end(&zip);
        return false;
    }

    uint64_t totalUncompressed = 0;
    constexpr uint64_t maxUncompressedTotal = 300ull * 1024ull * 1024ull;
    for (mz_uint i = 0; i < numFiles; ++i) {
        mz_zip_archive_file_stat stat;
        if (!mz_zip_reader_file_stat(&zip, i, &stat)) continue;

        std::string filename(stat.m_filename);
        if (filename.empty() || filename.back() == '/') continue;
        if (filename.find('\\') != std::string::npos) {
            error = "ZIP 包含无效路径: " + filename;
            mz_zip_reader_end(&zip);
            return false;
        }
        std::filesystem::path relativePath = std::filesystem::u8path(filename).lexically_normal();
        if (relativePath.empty() || relativePath.is_absolute()) {
            error = "ZIP 包含无效路径: " + filename;
            mz_zip_reader_end(&zip);
            return false;
        }
        for (const auto& part : relativePath) {
            if (part == "..") {
                error = "ZIP 包含越界路径: " + filename;
                mz_zip_reader_end(&zip);
                return false;
            }
        }
        if (stat.m_uncomp_size > 300ull * 1024ull * 1024ull) {
            error = "ZIP 内文件过大: " + filename;
            mz_zip_reader_end(&zip);
            return false;
        }
        totalUncompressed += static_cast<uint64_t>(stat.m_uncomp_size);
        if (totalUncompressed > maxUncompressedTotal) {
            error = "ZIP 解压后体积过大";
            mz_zip_reader_end(&zip);
            return false;
        }

        std::filesystem::path filePath = (dest / relativePath).lexically_normal();
        if (!isPathInside(filePath, dest)) {
            error = "ZIP 包含越界路径: " + filename;
            mz_zip_reader_end(&zip);
            return false;
        }
        std::filesystem::path fileDir = filePath.parent_path();

        std::error_code ec;
        if (!std::filesystem::exists(fileDir, ec)) {
            if (!std::filesystem::create_directories(fileDir, ec)) {
                error = "无法创建目录: " + u8toStr(fileDir);
                mz_zip_reader_end(&zip);
                return false;
            }
        }

        size_t uncompSize = 0;
        void* pFileData = mz_zip_reader_extract_to_heap(&zip, i, &uncompSize, 0);
        if (!pFileData) {
            error = "解压文件失败: " + filename;
            mz_zip_reader_end(&zip);
            return false;
        }

        std::string filePathStr = u8toStr(filePath);
        bool writeOk = writeFileFromBuffer(filePathStr, pFileData, uncompSize);
        mz_free(pFileData);

        if (!writeOk) {
            error = "写入文件失败: " + filename;
            mz_zip_reader_end(&zip);
            return false;
        }
    }

    mz_zip_reader_end(&zip);
    return true;
}

} // namespace JeriBot
