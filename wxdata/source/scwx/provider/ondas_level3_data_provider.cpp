#include <scwx/provider/ondas_level3_data_provider.hpp>
#include <scwx/types/ondas_types.hpp>
#include <scwx/util/logger.hpp>
#include <scwx/util/time.hpp>

#include <chrono>
#include <mutex>

#include <re2/re2.h>

namespace scwx::provider
{

static const std::string logPrefix_ =
   "scwx::provider::ondas_level3_data_provider";
static const auto logger_ = util::Logger::Create(logPrefix_);

static std::unordered_map<std::string, std::vector<std::string>> productMap_;
static std::shared_mutex                                         productMutex_;

class OndasLevel3DataProvider::Impl
{
public:
   explicit Impl(OndasLevel3DataProvider* self,
                 std::string              radarSite,
                 std::string              product,
                 std::string              baseUri) :
       self_(self),
       radarSite_(std::move(radarSite)),
       product_(std::move(product)),
       baseUri_(std::move(baseUri))
   {
   }
   ~Impl() = default;

   void ListProducts();

   Impl(const Impl&)             = delete;
   Impl& operator=(const Impl&)  = delete;
   Impl(const Impl&&)            = delete;
   Impl& operator=(const Impl&&) = delete;

   OndasLevel3DataProvider* self_;

   std::string radarSite_;
   std::string product_;
   std::string baseUri_;

   std::mutex listObjectsMutex_ {};
};

OndasLevel3DataProvider::OndasLevel3DataProvider(const std::string& radarSite,
                                                 const std::string& product,
                                                 const std::string& baseUri) :
    HttpNexradDataProvider(radarSite, baseUri),
    p(std::make_unique<Impl>(this, radarSite, product, baseUri))
{
}

OndasLevel3DataProvider::~OndasLevel3DataProvider() = default;

OndasLevel3DataProvider::OndasLevel3DataProvider(
   OndasLevel3DataProvider&&) noexcept = default;
OndasLevel3DataProvider& OndasLevel3DataProvider::operator=(
   OndasLevel3DataProvider&&) noexcept = default;

std::chrono::system_clock::time_point
OndasLevel3DataProvider::GetTimePointByKey(const std::string& key) const
{
   return GetTimePointFromKey(key);
}

std::chrono::system_clock::time_point
OndasLevel3DataProvider::GetTimePointFromKey(const std::string& key)
{
   // Filename/Timestamp Format (per ONDAS spec):
   // - Format: YYYYMMDD_HHMM (minimum) or SSSS_YYYYMMDD_HHMM
   // - Example: 20260131_1830 or KILN_20260131_1830
   // - Note: Some servers may include seconds or version suffix

   // The first 8 contiguous digits are used for the data (yyyymmdd), an
   // underscore, the next 4 contiguous digits are the time (hhmm) in 24
   // hour notation. Date and time MUST be in GMT/UTC timezone.

   // June 26, 2005 @ 11:45PM UTC would be 20050626_2145

   static constexpr re2::LazyRE2 re {R"((\d{8}_\d{4}))"};

   std::chrono::system_clock::time_point time {};
   std::string                           dateTimeStr {};

   if (!RE2::PartialMatch(key, *re, &dateTimeStr))
   {
      logger_->warn("Invalid ONDAS timestamp format in key: \"{}\"", key);
      return time;
   }

   // Match now contains the entire "YYYYMMDD_HHMM" substring
   // Parse using std::chrono::parse
   static const std::string timeFormat {"%Y%m%d_%H%M"};
   std::istringstream       ss(dateTimeStr);

   ss >> util::time::parse(timeFormat, time);

   if (ss.fail())
   {
      logger_->warn("Failed to parse ONDAS timestamp: \"{}\"", dateTimeStr);
   }

   return time;
}

std::tuple<bool, size_t, size_t>
OndasLevel3DataProvider::ListObjects(std::chrono::system_clock::time_point date)
{
   const std::string listingUrl = GetListingUrl(date);
   logger_->debug("ListObjects: {}", listingUrl);

   // Download dir.list
   const std::string content = DownloadToString(listingUrl);
   if (content.empty())
   {
      const std::unique_lock lock {p->listObjectsMutex_};
      ResetCacheStart();
      ResetCacheFinish();
      return {false, 0, 0};
   }

   // Parse ONDAS format
   const auto records = types::ondas::ParseOndasDirList(content);

   std::size_t newObjects   = 0;
   std::size_t totalObjects = 0;

   const std::unique_lock lock {p->listObjectsMutex_};

   ResetCacheStart();

   for (const auto& record : records)
   {
      const auto time = GetTimePointFromKey(record.filename_);
      if (time == std::chrono::system_clock::time_point {})
      {
         continue; // Invalid timestamp
      }

      // Add to cache (key is the filename)
      // Note: ONDAS doesn't provide lastModified, use file time as
      // approximation
      bool inserted = AddToCache(time, record.filename_, time);
      if (inserted)
      {
         newObjects++;
      }
      totalObjects++;
   }

   ResetCacheFinish();

   return {true, newObjects, totalObjects};
}

std::string OndasLevel3DataProvider::GetListingUrl(
   std::chrono::system_clock::time_point date)
{
   (void) date; // Not needed since ONDAS dir.list contains all dates

   // ONDAS directory listing URL format is:
   // {baseUri}/{radarSite}/{product}/dir.list
   return fmt::format("{0}/{1}/{2}/dir.list", p->baseUri_, p->radarSite_, p->product_);
}

std::string OndasLevel3DataProvider::GetFileUrl(const std::string& key)
{
   // ONDAS file URL format is:
   // {baseUri}/{radarSite}/{product}/{filename}
   return fmt::format("{0}/{1}/{2}/{3}", p->baseUri_, p->radarSite_, p->product_, key);
}

void OndasLevel3DataProvider::RequestAvailableProducts()
{
   p->ListProducts();
}

std::vector<std::string> OndasLevel3DataProvider::GetAvailableProducts()
{
   std::shared_lock readLock(productMutex_);

   auto siteProductMap = productMap_.find(p->radarSite_);
   if (siteProductMap != productMap_.cend())
   {
      return siteProductMap->second;
   }

   return {};
}

void OndasLevel3DataProvider::Impl::ListProducts()
{
   logger_->debug("ListProducts()");

   std::string data = "DAA DHR DOD DPA DPR DSD DSP DTA DU3 DU6 DVL EET HHC N0B N0C N0F N0G N0H N0K N0M N0Q N0R N0S N0U N0V N0X N0Z N1B N1C N1F N1G N1H N1K N1M N1P N1Q N1S N1U N1X N2B N2C N2F N2H N2K N2M N2Q N2S N2U N2X N3B N3C N3F N3H N3K N3M N3P N3Q N3S N3U N3X NAB NAC NAF NAG NAH NAK NAM NAQ NAU NAX NBB NBC NBF NBH NBK NBM NBQ NBU NBX NCR NCZ NET NHI NHL NLA NMD NML NRR NSS NST NSW NTP NTV NVL NVW OHA PTA RCM RSL SPD";
   std::stringstream ss(data);
   std::vector<std::string> productList;
   std::vector<std::string> productList(std::istream_iterator<std::string>{ss},
                                   std::istream_iterator<std::string>());
}

} // namespace scwx::provider
