#pragma once

#include <scwx/provider/http_nexrad_data_provider.hpp>

namespace scwx::provider
{

class OndasLevel3DataProvider : public HttpNexradDataProvider
{
public:
   explicit OndasLevel3DataProvider(const std::string& radarSite,
                                    const std::string& product,
                                    const std::string& baseUri);
   ~OndasLevel3DataProvider();

   OndasLevel3DataProvider(const OndasLevel3DataProvider&)            = delete;
   OndasLevel3DataProvider& operator=(const OndasLevel3DataProvider&) = delete;

   OndasLevel3DataProvider(OndasLevel3DataProvider&&) noexcept;
   OndasLevel3DataProvider& operator=(OndasLevel3DataProvider&&) noexcept;

   [[nodiscard]] std::chrono::system_clock::time_point
   GetTimePointByKey(const std::string& key) const override;

   static std::chrono::system_clock::time_point
   GetTimePointFromKey(const std::string& key);

   std::tuple<bool, size_t, size_t>
   ListObjects(std::chrono::system_clock::time_point date) override;

protected:
   std::string
   GetListingUrl(std::chrono::system_clock::time_point date) override;
   std::string GetFileUrl(const std::string& key) override;

private:
   class Impl;
   std::unique_ptr<Impl> p;
};

} // namespace scwx::provider
