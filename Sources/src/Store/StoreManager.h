#pragma once

#include "Json/Json.h"
#include "LLM/HttpClient.h"

#include <cstdint>
#include <string>
#include <vector>

namespace JeriBot {

class ConfigManager;

class StoreManager {
public:
    bool initialize(ConfigManager* configMgr, std::string& error);

    std::string fetchList(const std::string& type, std::string& error);

    bool installFromRemote(const std::string& type,
                           const std::string& id,
                           std::string& error);

    std::string previewLocalZipData(const std::string& zipData,
                                     std::string& outTempId,
                                     std::string& error);

    bool installFromLocal(const std::string& type,
                           const std::string& id,
                           const std::string& tempId,
                           std::string& error);

    bool uninstall(const std::string& type,
                    const std::string& id,
                    std::string& error);

    std::string getInstalled(const std::string& type, std::string& error);

    std::string getCurrentSource() const;

    std::string getSourceUrl() const;

    std::string getSourceRawUrl(const std::string& source) const;

    Json getSourcesList() const;

    bool setSource(const std::string& source, std::string& error);

    bool setCustomSourceUrl(const std::string& url, std::string& error);

    bool getInstalledIcon(const std::string& type,
                          const std::string& id,
                          std::vector<unsigned char>& outData,
                          std::string& error);

    bool getLocalZipIcon(const std::string& tempId,
                         std::vector<unsigned char>& outData,
                         std::string& error);

    bool getRemoteIcon(const std::string& type,
                       const std::string& id,
                       std::vector<unsigned char>& outData,
                       std::string& error);

private:
    ConfigManager* configMgr_ = nullptr;
    std::string configDir_;
    HttpClient http_;

    std::string typeToDirName(const std::string& type) const;
    std::string typeDir(const std::string& type) const;
    std::string loadJsonPath(const std::string& type) const;
    std::string packageDir(const std::string& type,
                           const std::string& uploader,
                           const std::string& id) const;

    Json loadLoadJson(const std::string& type);
    bool saveLoadJson(const std::string& type, const Json& data);

    bool extractZip(const std::string& zipPath,
                    const std::string& destDir,
                    std::string& error);

    bool installZipToPackage(const std::string& zipPath,
                             const std::string& type,
                             const std::string& id,
                             const std::string& uploader,
                             const Json& manifest,
                             std::string& error);

    std::string computeMd5(const std::string& filePath);

    bool findPackageInList(const std::string& type,
                           const std::string& id,
                           Json& outPackage,
                           std::string& error);

    bool fetchListDocument(const std::string& type,
                           Json& outList,
                           std::string* outBody,
                           std::string& error);

    std::string resolvePackageZipUrl(const Json& package,
                                     const std::string& baseUrl,
                                     std::string& error) const;

    std::string resolvePackageIconUrl(const Json& package,
                                      const std::string& baseUrl,
                                      std::string& error) const;

    Json buildLoadEntry(const Json& manifest, const std::string& id);
    Json extractManifestFromMemory(const void* data, size_t size, std::string& error);
    std::string ensureJeriTempDir(std::string& error);
    std::string tempZipPathFromId(const std::string& tempId, std::string& error) const;
};

} // namespace JeriBot
