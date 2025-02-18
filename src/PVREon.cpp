/*
 *  Copyright (C) 2011-2021 Team Kodi (https://kodi.tv)
 *  Copyright (C) 2011 Pulse-Eight (http://www.pulse-eight.com/)
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#include "PVREon.h"
#include "Globals.h"

#include <algorithm>

#include <kodi/General.h>
#include <kodi/gui/dialogs/OK.h>
#include "Utils.h"
#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"
#include "Base64.h"

#define CBC 1

#include "aes.hpp"
#include "pkcs7_padding.hpp"

static const uint8_t block_size = 16;

/***********************************************************
  * PVR Client AddOn specific public library functions
  ***********************************************************/

std::string ltrim(const std::string &s)
{
    size_t start = s.find_first_not_of("\"");
    return (start == std::string::npos) ? "" : s.substr(start);
}

std::string rtrim(const std::string &s)
{
    size_t end = s.find_last_not_of("\"");
    return (end == std::string::npos) ? "" : s.substr(0, end + 1);
}

std::string trim(const std::string &s) {
    return rtrim(ltrim(s));
}

std::string urlsafeencode(const std::string &s) {
  std::string t = s;
  std::replace( t.begin(), t.end(), '+', '-'); // replace all '+' to '-'
  std::replace( t.begin(), t.end(), '/', '_'); // replace all '/' to '_'
  t.erase(std::remove( t.begin(), t.end(), '='),
              t.end()); // remove padding
  return t;
}

std::string urlsafedecode(const std::string &s) {
  std::string t = s;
  std::replace( t.begin(), t.end(), '-', '+'); // replace all '-' to '+'
  std::replace( t.begin(), t.end(), '_', '/'); // replace all '_' to '/'
//  t.erase(std::remove( t.begin(), t.end(), '='),
//              t.end()); // remove padding
  return t;
}

std::string string_to_hex(const std::string& input)
{
    static const char hex_digits[] = "0123456789ABCDEF";

    std::string output;
    output.reserve(input.length() * 2);
    for (unsigned char c : input)
    {
        output.push_back(hex_digits[c >> 4]);
        output.push_back(hex_digits[c & 15]);
    }
    return output;
}

std::string aes_encrypt_cbc(const std::string &iv_str, const std::string &key, const std::string &plaintext)
{
    uint8_t i;

    int dlen = strlen(plaintext.c_str());
    int klen = strlen(key.c_str());

    //Proper Length of plaintext
    int dlenu = dlen;

    dlenu += block_size - (dlen % block_size);
    kodi::Log(ADDON_LOG_DEBUG, "The original length of the plaintext is = %d and the length of the padded plaintext is = %d", dlen, dlenu);

    // Make the uint8_t arrays
    uint8_t hexarray[dlenu];
    uint8_t kexarray[klen];
    uint8_t iv[klen];

    // Initialize them with zeros
    memset( hexarray, 0, dlenu );
    memset( kexarray, 0, klen );
    memset( iv, 0, klen );

    // Fill the uint8_t arrays
    for (int i=0;i<dlen;i++) {
        hexarray[i] = (uint8_t)plaintext[i];
    }
    for (int i=0;i<klen;i++) {
        kexarray[i] = (uint8_t)key[i];
        iv[i] = (uint8_t)iv_str[i];
    }

    pkcs7_padding_pad_buffer( hexarray, dlen, sizeof(hexarray), block_size );

    //start the encryption
    struct AES_ctx ctx;
    AES_init_ctx_iv(&ctx, kexarray, iv);

    // encrypt
    AES_CBC_encrypt_buffer(&ctx, hexarray, dlenu);

    std::ostringstream convert;
    for (int i = 0; i < dlenu; i++) {
        convert  hexarray[i];
    }
    std::string output = convert.str();

    return output;
}

bool CPVREon::GetPostJson(const std::string& url, const std::string& body, rapidjson::Document& doc)
{
  int statusCode = 0;
  std::string result;

  if (body.empty()) {
    result = m_httpClient->HttpGet(url, statusCode);
  } else
  {
//    kodi::Log(ADDON_LOG_DEBUG, "Body: %s", body.c_str());
    result = m_httpClient->HttpPost(url, body, statusCode);
  }
  //kodi::Log(ADDON_LOG_DEBUG, "Result: %s", result.c_str());
  doc.Parse(result.c_str());
  if ((doc.GetParseError()) || (statusCode != 200 && statusCode != 206))
  {
    kodi::Log(ADDON_LOG_ERROR, "Failed to get JSON for URL %s and body %s. Status code: %i", url.c_str(), body.c_str(), statusCode);
    if (!doc.GetParseError())
    {
      kodi::Log(ADDON_LOG_ERROR, "Result is: %s", result.c_str());
      if (doc.HasMember("error") && doc.HasMember("errorMessage"))
      {
        std::string title = Utils::JsonStringOrEmpty(doc, "error");
        std::string abstract = Utils::JsonStringOrEmpty(doc, "errorMessage");
        kodi::gui::dialogs::OK::ShowAndGetInput(title, abstract);
      }
    }
    return false;
  }
  return true;
}

int CPVREon::getBitrate(const bool isRadio, const int id) {
  for(unsigned int i = 0; i < m_rendering_profiles.size(); i++)
  {
    if (id == m_rendering_profiles[i].id) {
      if (isRadio) {
        return m_rendering_profiles[i].audioBitrate;
      } else {
        return m_rendering_profiles[i].videoBitrate;
      }
    }
  }
  return 0;
}

std::string CPVREon::getCoreStreamId(const int id) {
  for(unsigned int i = 0; i < m_rendering_profiles.size(); i++)
  {
    if (id == m_rendering_profiles[i].id) {
        return m_rendering_profiles[i].coreStreamId;
    }
  }
  return "";
}

int CPVREon::GetDefaultNumber(const bool isRadio, int id) {
  for(unsigned int i = 0; i < m_categories.size(); i++)
  {
    if (m_categories[i].isDefault && m_categories[i].isRadio == isRadio) {
      for(unsigned int j = 0; j < m_categories[i].channels.size(); j++) {
        if (m_categories[i].channels[j].id == id) {
          return m_categories[i].channels[j].position;
        }
      }
    }
  }
  return 0;
}

std::string CPVREon::GetBaseApi(const std::string& cdn_identifier) {

  for(int i=0; i < m_cdns.size(); i++){
    if (m_cdns[i].identifier == cdn_identifier) {
      return m_cdns[i].baseApi;
/*
      for (int j=0; j < m_cdns[i].domains.size(); j++){
        if (m_cdns[i].domains[j].name == "baseApi") {
          return m_cdns[i].domains[j].be;
        }
      }
*/
    }
  }

  return "";
}

std::string CPVREon::GetBrandIdentifier()
{
  std::string url = BROKER_URL + "v2/brands";

  rapidjson::Document doc;
  if (!GetPostJson(url, "", doc)) {
    kodi::Log(ADDON_LOG_ERROR, "Failed to get brands");
    return "";
  }

  int i = 0;
  int sp_id = m_settings->GetEonServiceProvider();
  kodi::Log(ADDON_LOG_DEBUG, "Requested Service Provider ID:%u", sp_id);

  const rapidjson::Value& brands = doc;

  for (rapidjson::Value::ConstValueIterator itr1 = brands.Begin();
      itr1 != brands.End(); ++itr1)
  {
    if (i == sp_id) {
      const rapidjson::Value& brandItem = (*itr1);
      return Utils::JsonStringOrEmpty(brandItem, "cdnIdentifier");
    }
    i++;
  }

  return "";
}

bool CPVREon::GetCDNInfo()
{
  std::string url = BROKER_URL + "v1/cdninfo";

  rapidjson::Document doc;
  if (!GetPostJson(url, "", doc)) {
    kodi::Log(ADDON_LOG_ERROR, "Failed to get cdninfo");
    return false;
  }
  const rapidjson::Value& cdns = doc;

  for (rapidjson::Value::ConstValueIterator itr1 = cdns.Begin();
      itr1 != cdns.End(); ++itr1)
  {
    const rapidjson::Value& cdnItem = (*itr1);

    EonCDN cdn;

    cdn.id = Utils::JsonIntOrZero(cdnItem, "id");
    cdn.identifier = Utils::JsonStringOrEmpty(cdnItem, "identifier");
    cdn.isDefault = Utils::JsonBoolOrFalse(cdnItem, "isDefault");

    const rapidjson::Value& baseApi = cdnItem["domains"]["baseApi"];

    cdn.baseApi = Utils::JsonStringOrEmpty(baseApi, EonParameters[m_platform].api_selector.c_str());
    m_cdns.emplace_back(cdn);
  }

  return true;
}

bool CPVREon::GetDeviceData()
{
  std::string url = m_support_web + "/gateway/SelfCareAPI/1.0/selfcareapi/" + SS_DOMAIN + "/subscriber/" + m_settings->GetSSIdentity() + "/devices/eon/2/product";

  rapidjson::Document doc;
  if (!GetPostJson(url, "", doc)) {
    kodi::Log(ADDON_LOG_ERROR, "Failed to get self service product data");
    return false;
  }

  std::string friendly_id;
  const rapidjson::Value& devices = doc["devices"];
  for (rapidjson::Value::ConstValueIterator itr1 = devices.Begin();
      itr1 != devices.End(); ++itr1)
  {
    const rapidjson::Value& device = (*itr1);
    m_device_serial = Utils::JsonStringOrEmpty(device, "serialNumber");
    m_device_id = std::to_string(Utils::JsonIntOrZero(device, "id"));
    m_device_number = Utils::JsonStringOrEmpty(device, "deviceNumber");
    friendly_id = Utils::JsonStringOrEmpty(device, "friendlyId");
    size_t found = friendly_id.find("web");
    if (found != std::string::npos) {
        kodi::Log(ADDON_LOG_DEBUG, "Got Device Serial Number: %s, DeviceID: %s, Device Number: %s", m_device_serial.c_str(), m_device_id.c_str(), m_device_number.c_str());
        return true;
    }
  }

  kodi::Log(ADDON_LOG_DEBUG, "Got Device Serial Number: %s, DeviceID: %s, Device Number: %s", m_device_serial.c_str(), m_device_id.c_str(), m_device_number.c_str());
  return true;
}

bool CPVREon::GetDeviceFromSerial()
{
  std::string postData;

  postData = "{\"deviceName\":\"" + EonParameters[m_platform].device_name +
                "\",\"deviceType\":\"" + EonParameters[m_platform].device_type +
                "\",\"modelName\":\"" + EonParameters[m_platform].device_model +
                "\",\"platform\":\"" + EonParameters[m_platform].device_platform +
                "\",\"serial\":\"" + m_device_serial +
                "\",\"clientSwVersion\":\"" + EonParameters[m_platform].client_sw_version;
  if (m_platform == PLATFORM_ANDROIDTV)
    postData += "\",\"clientSwBuild\":\"" + EonParameters[m_platform].client_sw_build;
  postData += "\",\"systemSwVersion\":{\"name\":\"" + EonParameters[m_platform].system_sw +
              "\",\"version\":\"" + EonParameters[m_platform].system_version + "\"}";
  if (m_platform == PLATFORM_ANDROIDTV)
    postData += ",\"fcmToken\":\"\""; //TODO: implement parameter fcmToken...
  postData += "}";

  std::string url = m_api + "v1/devices";

  rapidjson::Document doc;
  if (!GetPostJson(url, postData, doc)) {
    kodi::Log(ADDON_LOG_ERROR, "Failed to get devices");
    return false;
  }

  m_device_id = std::to_string(Utils::JsonIntOrZero(doc, "deviceId"));
  m_device_number = Utils::JsonStringOrEmpty(doc, "deviceNumber");
  kodi::Log(ADDON_LOG_DEBUG, "Got Device ID: %s and Device Number: %s", m_device_id.c_str(), m_device_number.c_str());

  return true;
}

bool CPVREon::GetServers()
{
  std::string url = m_api + "v1/servers";

  rapidjson::Document doc;
  if (!GetPostJson(url, "", doc)) {
    kodi::Log(ADDON_LOG_ERROR, "Failed to get servers");
    return false;
  }

  const rapidjson::Value& liveServers = doc["live_servers"];
  for (rapidjson::Value::ConstValueIterator itr1 = liveServers.Begin();
      itr1 != liveServers.End(); ++itr1)
  {
    const rapidjson::Value& liveServer = (*itr1);
    EonServer eon_server;

    eon_server.id = Utils::JsonStringOrEmpty(liveServer, "id");
    eon_server.ip = Utils::JsonStringOrEmpty(liveServer, "ip");
    eon_server.hostname = Utils::JsonStringOrEmpty(liveServer, "hostname");

    kodi::Log(ADDON_LOG_DEBUG, "Got Live Server: %s %s %s", eon_server.id.c_str(), eon_server.ip.c_str(), eon_server.hostname.c_str());
    m_live_servers.emplace_back(eon_server);
  }

  const rapidjson::Value& timeshiftServers = doc["timeshift_servers"];
  for (rapidjson::Value::ConstValueIterator itr1 = timeshiftServers.Begin();
      itr1 != timeshiftServers.End(); ++itr1)
  {
    const rapidjson::Value& server = (*itr1);
    EonServer eon_server;

    eon_server.id = Utils::JsonStringOrEmpty(server, "id");
    eon_server.ip = Utils::JsonStringOrEmpty(server, "ip");
    eon_server.hostname = Utils::JsonStringOrEmpty(server, "hostname");

    kodi::Log(ADDON_LOG_DEBUG, "Got Timeshift Server: %s %s %s", eon_server.id.c_str(), eon_server.ip.c_str(), eon_server.hostname.c_str());
    m_timeshift_servers.emplace_back(eon_server);
  }

  return true;
}


bool CPVREon::GetHouseholds()
{
  std::string url = m_api + "v1/households";

  rapidjson::Document doc;
  if (!GetPostJson(url, "", doc)) {
    kodi::Log(ADDON_LOG_ERROR, "Failed to get households");
    return false;
  }

  m_subscriber_id = std::to_string(Utils::JsonIntOrZero(doc, "id"));
  kodi::Log(ADDON_LOG_DEBUG, "Got Subscriber ID: %s", m_subscriber_id.c_str());

  return true;
}

std::string CPVREon::GetTime()
{
  std::string url = m_api + "v1/time";

  rapidjson::Document doc;
  if (!GetPostJson(url, "", doc)) {
    kodi::Log(ADDON_LOG_ERROR, "Failed to get time");
    return "";
  }

  return Utils::JsonStringOrEmpty(doc, "time");
}

bool CPVREon::GetServiceProvider()
{
  std::string url = m_api + "v1/sp";

  rapidjson::Document doc;
  if (!GetPostJson(url, "", doc)) {
    kodi::Log(ADDON_LOG_ERROR, "Failed to get service provider");
    return false;
  }

  m_service_provider = Utils::JsonStringOrEmpty(doc, "identifier");
  m_support_web = Utils::JsonStringOrEmpty(doc, "supportWebAddress");
  m_httpClient->SetSupportApi(m_support_web);
  kodi::Log(ADDON_LOG_DEBUG, "Got Service Provider: %s and Support Web: %s", m_service_provider.c_str(), m_support_web.c_str());

  return true;
}

bool CPVREon::GetRenderingProfiles()
{
  std::string url = m_api + "v1/rndprofiles";

  rapidjson::Document doc;
  if (!GetPostJson(url, "", doc)) {
    kodi::Log(ADDON_LOG_ERROR, "Failed to get rendering profiles");
    return false;
  }

  const rapidjson::Value& rndProfiles = doc;

  for (rapidjson::Value::ConstValueIterator itr1 = rndProfiles.Begin();
      itr1 != rndProfiles.End(); ++itr1)
  {
    const rapidjson::Value& rndProfileItem = (*itr1);

    EonRenderingProfile eon_rndprofile;

    eon_rndprofile.id = Utils::JsonIntOrZero(rndProfileItem, "id");
    eon_rndprofile.name = Utils::JsonStringOrEmpty(rndProfileItem, "name");
    eon_rndprofile.coreStreamId = Utils::JsonStringOrEmpty(rndProfileItem, "coreStreamId");
    eon_rndprofile.height = Utils::JsonIntOrZero(rndProfileItem, "height");
    eon_rndprofile.width = Utils::JsonIntOrZero(rndProfileItem, "width");
    eon_rndprofile.audioBitrate = Utils::JsonIntOrZero(rndProfileItem, "audioBitrate");
    eon_rndprofile.videoBitrate = Utils::JsonIntOrZero(rndProfileItem, "videoBitrate");

    m_rendering_profiles.emplace_back(eon_rndprofile);
  }
  return true;
}

bool CPVREon::GetCategories(const bool isRadio)
{
  std::string url = m_api + "v2/categories/" + (isRadio ? "RADIO" : "TV");

  rapidjson::Document doc;
  if (!GetPostJson(url, "", doc)) {
    kodi::Log(ADDON_LOG_ERROR, "Failed to get categories");
    return false;
  }

  const rapidjson::Value& categories = doc;

  for (rapidjson::Value::ConstValueIterator itr1 = categories.Begin();
      itr1 != categories.End(); ++itr1)
  {
    const rapidjson::Value& categoryItem = (*itr1);

    EonCategory eon_category;

    eon_category.id = Utils::JsonIntOrZero(categoryItem, "id");
    eon_category.order = Utils::JsonIntOrZero(categoryItem, "order");
    eon_category.name = Utils::JsonStringOrEmpty(categoryItem, "name");
    eon_category.isRadio = isRadio;
    eon_category.isDefault = Utils::JsonBoolOrFalse(categoryItem, "defaultList");

    const rapidjson::Value& channels = categoryItem["channels"];
    for (rapidjson::Value::ConstValueIterator itr2 = channels.Begin();
        itr2 != channels.End(); ++itr2)
    {
      const rapidjson::Value& channelItem = (*itr2);

      EonCategoryChannel eon_category_channel;

      eon_category_channel.id = Utils::JsonIntOrZero(channelItem, "id");
      eon_category_channel.position = Utils::JsonIntOrZero(channelItem, "position");

      eon_category.channels.emplace_back(eon_category_channel);
    }
    m_categories.emplace_back(eon_category);
  }
  return true;
}

CPVREon::CPVREon() :
  m_settings(new CSettings())
{
  m_settings->Load();
  m_httpClient = new HttpClient(m_settings);

  m_platform = m_settings->GetPlatform();
  m_support_web = "API_NOT_SET_YET";
  m_httpClient->SetSupportApi(m_support_web);

  srand(time(nullptr));

  if (GetCDNInfo()) {
    std::string cdn_identifier = GetBrandIdentifier();
    kodi::Log(ADDON_LOG_DEBUG, "CDN Identifier: %s", cdn_identifier.c_str());
    std::string baseApi = GetBaseApi(cdn_identifier);
    m_api = "https://api-" + EonParameters[m_platform].api_prefix + "." + baseApi + "/";
    m_images_api = "https://images-" + EonParameters[m_platform].api_prefix + "." + baseApi + "/";
  } else {
    m_api = "https://api-" + EonParameters[m_platform].api_prefix + "." + GLOBAL_URL;
    m_images_api = "https://images-" + EonParameters[m_platform].api_prefix + "." + GLOBAL_URL;
  }
  m_httpClient->SetApi(m_api);
  kodi::Log(ADDON_LOG_DEBUG, "API set to: %s", m_api.c_str());

  m_device_id = m_settings->GetEonDeviceID();
  m_device_number = m_settings->GetEonDeviceNumber();
/*
  m_ss_identity = m_settings->GetSSIdentity();
  if (m_ss_identity.empty()) {
    m_httpClient->RefreshSSToken();
    m_ss_identity = m_settings->GetSSIdentity();
  }
*/
  if (m_device_id.empty() || m_device_number.empty()) {
/*
    if (GetDeviceData()) {
      m_settings->SetSetting("deviceid", m_device_id);
      m_settings->SetSetting("devicenumber", m_device_number);
      m_settings->SetSetting("deviceserial", m_device_serial);
    }
*/
    m_device_serial = m_settings->GetEonDeviceSerial();
    if (m_device_serial.empty()) {
      m_device_serial = m_httpClient->GetUUID();
      m_settings->SetSetting("deviceserial", m_device_serial);
      kodi::Log(ADDON_LOG_DEBUG, "Generated Device Serial: %s", m_device_serial.c_str());
    }
    if (GetDeviceFromSerial()) {
      m_settings->SetSetting("deviceid", m_device_id);
      m_settings->SetSetting("devicenumber", m_device_number);
    }
  }

  bool allgood = true;

  m_subscriber_id = m_settings->GetEonSubscriberID();
  if (m_subscriber_id.empty()) {
    if (GetHouseholds()) {
      m_settings->SetSetting("subscriberid", m_subscriber_id);
    } else {
      allgood = false;
    }
  }

  if (m_service_provider.empty() || m_support_web.empty()) {
    allgood = GetServiceProvider();
  }

  if (allgood) {
    allgood = GetRenderingProfiles();
  }
  if (allgood) {
    allgood = GetServers();
  }
  if (m_settings->IsTVenabled() && (allgood)) {
    allgood = GetCategories(false);
    allgood = LoadChannels(false);
  }
  if (m_settings->IsRadioenabled() && (allgood)) {
    allgood = GetCategories(true);
    allgood = LoadChannels(true);
  }
}

CPVREon::~CPVREon()
{
  m_channels.clear();
}

ADDON_STATUS CPVREon::SetSetting(const std::string& settingName, const std::string& settingValue)
{
  ADDON_STATUS result = m_settings->SetSetting(settingName, settingValue);
  if (!m_settings->VerifySettings()) {
    return ADDON_STATUS_NEED_SETTINGS;
  }
  return result;
}

bool CPVREon::LoadChannels(const bool isRadio)
{
  kodi::Log(ADDON_LOG_DEBUG, "Load Eon Channels");

  std::string url = m_api + "v3/channels?channelType=" + (isRadio ? "RADIO&channelSort=RECOMMENDED&sortDir=DESC" : "TV");

  rapidjson::Document doc;
  if (!GetPostJson(url, "", doc)) {
    kodi::Log(ADDON_LOG_ERROR, "Failed to load channels");
    return false;
  }

  int currentnumber = 0;
  int startnumber = m_settings->GetStartNum()-1;
  int lastnumber = startnumber;
  const rapidjson::Value& channels = doc;

  for (rapidjson::Value::ConstValueIterator itr1 = channels.Begin();
      itr1 != channels.End(); ++itr1)
  {
    const rapidjson::Value& channelItem = (*itr1);

    std::string channame;
    if (m_settings->UseShortNames()) {
      channame = Utils::JsonStringOrEmpty(channelItem, "shortName");
    } else {
      channame = Utils::JsonStringOrEmpty(channelItem, "name");
    }

    EonChannel eon_channel;

    eon_channel.bRadio = isRadio;
    eon_channel.bArchive = Utils::JsonBoolOrFalse(channelItem, "cutvEnabled");
    eon_channel.strChannelName = channame;
    int ref_id = Utils::JsonIntOrZero(channelItem, "id");

    currentnumber = startnumber + GetDefaultNumber(isRadio, ref_id);
    if (currentnumber != 0) {
      eon_channel.iChannelNumber = currentnumber;
      lastnumber = currentnumber++;
    } else {
      eon_channel.iChannelNumber = ++lastnumber;
    }

    eon_channel.iUniqueId = ref_id;

    eon_channel.aaEnabled = Utils::JsonBoolOrFalse(channelItem, "aaEnabled");
    eon_channel.subscribed = Utils::JsonBoolOrFalse(channelItem, "subscribed");
    int ageRating = 0;
    try {
      ageRating = std::stoi(Utils::JsonStringOrEmpty(channelItem,"ageRating"));
    } catch (std::invalid_argument&e) {

    }
    eon_channel.ageRating = ageRating;
    const rapidjson::Value& categories = channelItem["categories"];
    for (rapidjson::Value::ConstValueIterator itr2 = categories.Begin();
        itr2 != categories.End(); ++itr2)
    {
      const rapidjson::Value& categoryItem = (*itr2);

      EonChannelCategory cat;

      cat.id = Utils::JsonIntOrZero(categoryItem, "id");
      cat.primary = Utils::JsonBoolOrFalse(categoryItem, "primary");

      eon_channel.categories.emplace_back(cat);
    }
    const rapidjson::Value& images = channelItem["images"];
    for (rapidjson::Value::ConstValueIterator itr2 = images.Begin();
        itr2 != images.End(); ++itr2)
    {
      const rapidjson::Value& imageItem = (*itr2);

      if (Utils::JsonStringOrEmpty(imageItem, "size") == "XL") {
        eon_channel.strIconPath = m_images_api + Utils::JsonStringOrEmpty(imageItem, "path");
      }
    }
    const rapidjson::Value& pp = channelItem["publishingPoint"];
    for (rapidjson::Value::ConstValueIterator itr2 = pp.Begin();
        itr2 != pp.End(); ++itr2)
    {
      const rapidjson::Value& ppItem = (*itr2);

      EonPublishingPoint pp;

      pp.publishingPoint = Utils::JsonStringOrEmpty(ppItem, "publishingPoint");
      pp.audioLanguage =  Utils::JsonStringOrEmpty(ppItem, "audioLanguage");
      pp.subtitleLanguage =  Utils::JsonStringOrEmpty(ppItem, "subtitleLanguage");

      const rapidjson::Value& profileIds = ppItem["profileIds"];
      for (rapidjson::Value::ConstValueIterator itr3 = profileIds.Begin();
          itr3 != profileIds.End(); ++itr3)
      {
        pp.profileIds.emplace_back(itr3->GetInt());
      }

      const rapidjson::Value& playerCfgs = ppItem["playerCfgs"];
      for (rapidjson::Value::ConstValueIterator itr3 = playerCfgs.Begin();
          itr3 != playerCfgs.End(); ++itr3)
      {
        const rapidjson::Value& playerCfgItem = (*itr3);
        if (Utils::JsonStringOrEmpty(playerCfgItem, "type") ==  "live") {
          eon_channel.sig = Utils::JsonStringOrEmpty(playerCfgItem, "sig");
        }
      }
      eon_channel.publishingPoints.emplace_back(pp);
    }

    if (!m_settings->HideUnsubscribed() || eon_channel.subscribed) {
      kodi::Log(ADDON_LOG_DEBUG, "%i. Channel Name: %s ID: %i Sig: %s", lastnumber, channame.c_str(), ref_id, eon_channel.sig.c_str());
      m_channels.emplace_back(eon_channel);
    }
  }

  return true;
}

bool CPVREon::HandleSession(bool start, int cid, int epg_id)
{
  std::string time = GetTime();
  std::string epoch = time.substr(0,10);
  std::string ms = time.substr(10,3);
  time_t timestamp = atoll(epoch.c_str());
  std::string datetime = Utils::TimeToString(timestamp) + "." + ms + "Z";
  std::string offset = Utils::TimeToString(timestamp-300) + "." + ms + "Z";
  kodi::Log(ADDON_LOG_DEBUG, "Handle Session time: %s", datetime.c_str());
  std::string action;
  if (start) {
    action = "start";
  }
  else {
    action = "stop";
  }

  std::string postData = "[{\"time\":\"" + datetime +
                         "\",\"type\":\"CUTV\",\"rnd_profile\":\"hp7000\",\"session_id\":\"" + m_session_id +
                         "\",\"action\":\"" + action +
                         "\",\"device\":{\"id\":" + m_device_id +
                         "},\"subscriber\":{\"id\":" + m_subscriber_id +
                         "},\"offset_time\":\"" + offset +
                         "\",\"channel\":{\"id\":" + std::to_string(cid) +
                         "},\"epg_event_id\":" + std::to_string(epg_id) +
                         ",\"viewing_time\":500,\"silent_event_change\":false}]";
  kodi::Log(ADDON_LOG_DEBUG, "Session PostData: %s", postData.c_str());

  std::string url = m_api + "v1/events";

  rapidjson::Document doc;
  if (!GetPostJson(url, postData, doc)) {
    kodi::Log(ADDON_LOG_ERROR, "Failed to register event");
    return false;
  }

  return Utils::JsonBoolOrFalse(doc, "success");
}

void CPVREon::SetStreamProperties(std::vector<kodi::addon::PVRStreamProperty>& properties,
                                    const std::string& url,
                                    const bool& realtime, const bool& playTimeshiftBuffer, const bool& isLive/*,
                                    time_t starttime, time_t endtime*/)
{
  kodi::Log(ADDON_LOG_DEBUG, "[PLAY STREAM] url: %s", url.c_str());

  properties.emplace_back(PVR_STREAM_PROPERTY_STREAMURL, url);
  properties.emplace_back(PVR_STREAM_PROPERTY_ISREALTIMESTREAM, realtime ? "true" : "false");

  int inputstream = m_settings->GetInputstream();

  if (inputstream == INPUTSTREAM_ADAPTIVE)
  {
    if (!Utils::CheckInputstreamInstalledAndEnabled("inputstream.adaptive"))
    {
      kodi::Log(ADDON_LOG_DEBUG, "inputstream.adaptive selected but not installed or enabled");
      return;
    }
    kodi::Log(ADDON_LOG_DEBUG, "...using inputstream.adaptive");
    properties.emplace_back(PVR_STREAM_PROPERTY_INPUTSTREAM, "inputstream.adaptive");
    properties.emplace_back("inputstream.adaptive.manifest_type", "hls");
    //  properties.emplace_back("inputstream.adaptive.original_audio_language", "bs");
    //  properties.emplace_back("inputstream.adaptive.stream_selection_type", "adaptive");
    // properties.emplace_back("inputstream.adaptive.stream_selection_type", "manual-osd");
    properties.emplace_back("inputstream.adaptive.stream_selection_type", "fixed-res");
    properties.emplace_back("inputstream.adaptive.chooser_resolution_max", "4K");
    //properties.emplace_back("inputstream.adaptive.stream_selection_type", "fixed-res");
    properties.emplace_back("inputstream.adaptive.manifest_headers", "User-Agent=" + EonParameters[m_platform].user_agent);
    // properties.emplace_back("inputstream.adaptive.manifest_update_parameter", "full");
  } else if (inputstream == INPUTSTREAM_FFMPEGDIRECT)
  {
    if (!Utils::CheckInputstreamInstalledAndEnabled("inputstream.ffmpegdirect"))
    {
      kodi::Log(ADDON_LOG_DEBUG, "inputstream.ffmpegdirect selected but not installed or enabled");
      return;
    }
    kodi::Log(ADDON_LOG_DEBUG, "...using inputstream.ffmpegdirect");
    properties.emplace_back(PVR_STREAM_PROPERTY_INPUTSTREAM, "inputstream.ffmpegdirect");
    properties.emplace_back("inputstream.ffmpegdirect.manifest_type", "hls");
    properties.emplace_back("inputstream.ffmpegdirect.is_realtime_stream", "true");
    properties.emplace_back("inputstream.ffmpegdirect.stream_mode", isLive ? "timeshift" : "catchup");
/*
    if (!isLive) {
      properties.emplace_back("inputstream.ffmpegdirect.catchup_buffer_start_time", std::to_string(starttime));
      properties.emplace_back("inputstream.ffmpegdirect.catchup_buffer_end_time", std::to_string(endtime));
      properties.emplace_back("inputstream.ffmpegdirect.programme_start_time", std::to_string(starttime));
      properties.emplace_back("inputstream.ffmpegdirect.programme_end_time", std::to_string(endtime));
    }
*/
  } else {
    kodi::Log(ADDON_LOG_DEBUG, "Unknown inputstream detected");
  }
  properties.emplace_back(PVR_STREAM_PROPERTY_MIMETYPE, "application/x-mpegURL");
}

PVR_ERROR CPVREon::GetCapabilities(kodi::addon::PVRCapabilities& capabilities)
{
  capabilities.SetSupportsEPG(true);
  capabilities.SetSupportsTV(m_settings->IsTVenabled());
  capabilities.SetSupportsRadio(m_settings->IsRadioenabled());
  capabilities.SetSupportsChannelGroups(m_settings->IsGroupsenabled());
  capabilities.SetSupportsRecordings(false);
  capabilities.SetSupportsRecordingsDelete(false);
  capabilities.SetSupportsRecordingsUndelete(false);
  capabilities.SetSupportsTimers(false);
  capabilities.SetSupportsRecordingsRename(false);
  capabilities.SetSupportsRecordingsLifetimeChange(false);
  capabilities.SetSupportsDescrambleInfo(false);
  capabilities.SetSupportsProviders(false);

  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVREon::GetBackendName(std::string& name)
{
  name = "EON PVR";
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVREon::GetBackendVersion(std::string& version)
{
  version = STR(EON_VERSION);
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVREon::GetConnectionString(std::string& connection)
{
  connection = "connected";
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVREon::GetBackendHostname(std::string& hostname)
{
  hostname = m_api;
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVREon::GetDriveSpace(uint64_t& total, uint64_t& used)
{
  total = 0;
  used = 0;
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVREon::GetEPGForChannel(int channelUid,
                                     time_t start,
                                     time_t end,
                                     kodi::addon::PVREPGTagsResultSet& results)
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);
  for (const auto& channel : m_channels)
  {

    if (channel.iUniqueId != channelUid)
      continue;

    kodi::Log(ADDON_LOG_DEBUG, "EPG Request for Channel %u Start %u End %u", channel.iUniqueId, start, end);

    std::string url = m_api + "v1/events/epg" +
                              "?cid=" + std::to_string(channel.iUniqueId) +
                              "&fromTime=" + std::to_string(start) + "000" +
                              "&toTime=" + std::to_string(end) + "000";

    rapidjson::Document epgDoc;
    if (!GetPostJson(url, "", epgDoc)) {
      kodi::Log(ADDON_LOG_ERROR, "[GetEPG] ERROR: error while parsing json");
      return PVR_ERROR_SERVER_ERROR;
    }

    kodi::Log(ADDON_LOG_DEBUG, "[epg] iterate entries");

//    std::string cid = "\"" + std::to_string(channel.referenceID) + "\"";
    std::string cid = std::to_string(channel.iUniqueId);
//    kodi::Log(ADDON_LOG_DEBUG, "EPG Channel ReferenceID: %s", cid.c_str());
    const rapidjson::Value& epgitems = epgDoc[cid.c_str()];
//    kodi::Log(ADDON_LOG_DEBUG, "EPG Items: %s", epgitems.c_str());
    for (rapidjson::Value::ConstValueIterator itr1 = epgitems.Begin();
        itr1 != epgitems.End(); ++itr1)
    {
      const rapidjson::Value& epgItem = (*itr1);

      kodi::addon::PVREPGTag tag;
      unsigned int epg_tag_flags = EPG_TAG_FLAG_UNDEFINED;

      tag.SetUniqueBroadcastId(Utils::JsonIntOrZero(epgItem,"id"));
      tag.SetUniqueChannelId(channelUid);
      tag.SetTitle(Utils::JsonStringOrEmpty(epgItem,"title"));
      tag.SetOriginalTitle(Utils::JsonStringOrEmpty(epgItem,"originalTitle"));
      time_t starttime = (time_t) (Utils::JsonInt64OrZero(epgItem,"startTime") / 1000);
      time_t endtime = (time_t) (Utils::JsonInt64OrZero(epgItem,"endTime") / 1000);
      tag.SetStartTime(starttime);
      tag.SetEndTime(endtime);
      tag.SetPlot(Utils::JsonStringOrEmpty(epgItem,"shortDescription"));
      int seasonNumber = Utils::JsonIntOrZero(epgItem,"seasonNumber");
      if (seasonNumber != 0)
        tag.SetSeriesNumber(seasonNumber);
      int episodeNumber = Utils::JsonIntOrZero(epgItem,"episodeNumber");
      if (episodeNumber != 0)
      {
        tag.SetEpisodeNumber(episodeNumber);
        epg_tag_flags += EPG_TAG_FLAG_IS_SERIES;
      }
      int ageRating = 0;
      try {
        ageRating = std::stoi(Utils::JsonStringOrEmpty(epgItem,"ageRating"));
      } catch (std::invalid_argument&e) {

      }
      if (ageRating != 0)
        tag.SetParentalRating(ageRating);

      if (Utils::JsonBoolOrFalse(epgItem, "liveBroadcast"))
        epg_tag_flags += EPG_TAG_FLAG_IS_LIVE;

      const rapidjson::Value& images = epgItem["images"];
      for (rapidjson::Value::ConstValueIterator itr2 = images.Begin();
          itr2 != images.End(); ++itr2)
      {
        const rapidjson::Value& imageItem = (*itr2);

        if (Utils::JsonStringOrEmpty(imageItem, "size") == "STB_XL") {
          tag.SetIconPath(m_images_api + Utils::JsonStringOrEmpty(imageItem, "path"));
        }
      }
/*
      const rapidjson::Value& categories = epgItem["categories"];
      for (rapidjson::SizeType i = 0; i < categories.Size(); i++)
      {
        kodi::Log(ADDON_LOG_DEBUG, "Category: %u", categories[i].GetInt());
      }
*/
//      kodi::Log(ADDON_LOG_DEBUG, "%u adding EPG: ID: %u Title: %s Start: %u End: %u", channelUid, Utils::JsonIntOrZero(epgItem,"id"), Utils::JsonStringOrEmpty(epgItem,"title").c_str(),Utils::JsonIntOrZero(epgItem,"startTime")/1000,Utils::JsonIntOrZero(epgItem,"endTime")/1000);
      tag.SetFlags(epg_tag_flags);
      results.Add(tag);
    }
  }

  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVREon::IsEPGTagPlayable(const kodi::addon::PVREPGTag& tag, bool& bIsPlayable)
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);
  bIsPlayable = false;

  for (const auto& channel : m_channels)
  {
    if (channel.iUniqueId == tag.GetUniqueChannelId())
    {
      auto current_time = time(NULL);
      if (current_time > tag.GetStartTime())
      {
        bIsPlayable = channel.bArchive;
      }
    }
  }

  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVREon::GetEPGTagStreamProperties(
    const kodi::addon::PVREPGTag& tag, std::vector<kodi::addon::PVRStreamProperty>& properties)
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);
  for (const auto& channel : m_channels)
  {
    if (channel.iUniqueId == tag.GetUniqueChannelId())
    {
      return GetStreamProperties(channel, properties, tag.GetStartTime(), /*tag.GetEndTime(),*/ false);
    }
  }
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVREon::GetProvidersAmount(int& amount)
{
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVREon::GetProviders(kodi::addon::PVRProvidersResultSet& results)
{
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVREon::GetChannelsAmount(int& amount)
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);
  amount = m_channels.size();
  std::string amount_str = std::to_string(amount);
  kodi::Log(ADDON_LOG_DEBUG, "Channels Amount: [%s]", amount_str.c_str());
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVREon::GetChannels(bool bRadio, kodi::addon::PVRChannelsResultSet& results)
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);
  for (const auto& channel : m_channels)
  {

    int ageRating = m_settings->GetAgeRating();
    if (channel.bRadio == bRadio && ageRating >= channel.ageRating)
    {
      kodi::addon::PVRChannel kodiChannel;

      kodiChannel.SetUniqueId(channel.iUniqueId);
      kodiChannel.SetIsRadio(channel.bRadio);
      kodiChannel.SetChannelNumber(channel.iChannelNumber);
//      kodiChannel.SetSubChannelNumber(channel.iSubChannelNumber);
      kodiChannel.SetChannelName(channel.strChannelName);
//      kodiChannel.SetEncryptionSystem(channel.iEncryptionSystem);
      kodiChannel.SetIconPath(channel.strIconPath);
      kodiChannel.SetIsHidden(false);
      kodiChannel.SetHasArchive(channel.bArchive);

//       PVR API 8.0.0
//      kodiChannel.SetClientProviderUid(channel.iProviderId);

      results.Add(kodiChannel);
    }
  }

  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVREon::GetStreamProperties(
    const EonChannel& channel, std::vector<kodi::addon::PVRStreamProperty>& properties, time_t starttime,/* time_t endtime,*/ const bool& isLive)
{
    kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);
    std::string streaming_profile = "hp7000";

    unsigned int rndbitrate = 0;
    unsigned int current_bitrate = 0;
    unsigned int current_id = 0;
    for (unsigned int i = 0; i < channel.publishingPoints[0].profileIds.size(); i++) {
        current_bitrate = getBitrate(channel.bRadio, channel.publishingPoints[0].profileIds[i]);
        kodi::Log(ADDON_LOG_DEBUG, "Bitrate is: %u for profile id: %u", current_bitrate, channel.publishingPoints[0].profileIds[i]);
        if (current_bitrate > rndbitrate) {
          current_id = channel.publishingPoints[0].profileIds[i];
          rndbitrate = current_bitrate;
        }
    }
    if (current_id == 0) {
      return PVR_ERROR_NO_ERROR;
    } else {
      streaming_profile = getCoreStreamId(current_id);
    }
    kodi::Log(ADDON_LOG_DEBUG, "Channel Rendering Profile -> %u", current_id);

    m_session_id = Utils::CreateUUID();

    EonServer currentServer;

    GetServer(isLive, currentServer);

    std::string plain_aes;

    if (m_platform == PLATFORM_ANDROIDTV) {
      plain_aes = "channel=" + channel.publishingPoints[0].publishingPoint + ";" +
                  "stream=" + streaming_profile + ";" +
                  "sp=" + m_service_provider + ";" +
                  "u=" + m_settings->GetEonStreamUser() + ";" +
                  "m=" + currentServer.ip + ";" +
                  "device=" + m_settings->GetEonDeviceNumber() + ";" +
                  "ctime=" + GetTime() + ";" +
//                  "lang=eng;minvbr=100;adaptive=true;player=" + PLAYER + ";" +
                  "lang=eng;player=" + PLAYER + ";" +
                  "aa=" + (channel.aaEnabled ? "true" : "false") + ";" +
                  "conn=" + CONN_TYPE_ETHERNET + ";" +
                  "minvbr=100;" +
//                  "sig=" + channel.sig + ";" +
                  "ss=" + m_settings->GetEonStreamKey() + ";" +
                  "session=" + m_session_id + ";" +
                  "maxvbr=" + std::to_string(rndbitrate);
                  if (!isLive) {
                    plain_aes = plain_aes + ";t=" + std::to_string((int) starttime) + "000;";
                  }
    } else {
      plain_aes = "channel=" + channel.publishingPoints[0].publishingPoint + ";" +
                  "stream=" + streaming_profile + ";" + "sp=" + m_service_provider + ";" +
                  "u=" + m_settings->GetEonStreamUser() + ";" +
                  "ss=" + m_settings->GetEonStreamKey() + ";" +
                  "minvbr=100;adaptive=true;player=" + PLAYER + ";" +
                  "sig=" + channel.sig + ";" +
                  "session=" + m_session_id + ";" +
                  "m=" + currentServer.ip + ";" +
                  "device=" + m_settings->GetEonDeviceNumber() + ";" +
                  "ctime=" + GetTime() + ";" +
                  "conn=" + CONN_TYPE_BROWSER + ";";
                  if (!isLive) {
                    plain_aes = plain_aes + "t=" + std::to_string((int) starttime) + "000;";
                  }
                  plain_aes = plain_aes + "aa=" + (channel.aaEnabled ? "true" : "false");
    }
//    kodi::Log(ADDON_LOG_DEBUG, "Plain AES -> %s", plain_aes.c_str());

    std::string key = base64_decode(urlsafedecode(m_settings->GetEonStreamKey()));

    std::ostringstream convert;
    for (int i = 0; i < block_size; i++) {
        convert  (uint8_t) rand();
    }
    std::string iv_str = convert.str();

    std::string enc_str = aes_encrypt_cbc(iv_str, key, plain_aes);

    kodi::Log(ADDON_LOG_DEBUG, "IV -> %s", string_to_hex(iv_str).c_str());
    kodi::Log(ADDON_LOG_DEBUG, "IV (base64) -> %s", urlsafeencode(base64_encode(iv_str.c_str(), iv_str.length())).c_str());
//    kodi::Log(ADDON_LOG_DEBUG, "Key -> %s", string_to_hex(key).c_str());
    kodi::Log(ADDON_LOG_DEBUG, "Encrypted -> %s", string_to_hex(enc_str).c_str());
    kodi::Log(ADDON_LOG_DEBUG, "Encrypted (base64) -> %s", urlsafeencode(base64_encode(enc_str.c_str(), enc_str.length())).c_str());

    std::string enc_url = "https://" + currentServer.hostname +
                          "/stream?i=" + urlsafeencode(base64_encode(iv_str.c_str(), iv_str.length())) +
                          "&a=" + urlsafeencode(base64_encode(enc_str.c_str(), enc_str.length()));
    if (m_platform == PLATFORM_ANDROIDTV) {
      enc_url = enc_url + "&lang=eng";
    }
    enc_url = enc_url +   "&sp=" + m_service_provider +
                          "&u=" + m_settings->GetEonStreamUser() +
                          "&player=" + PLAYER +
                          "&session=" + m_session_id;
    if (m_platform != PLATFORM_ANDROIDTV) {
      enc_url = enc_url + "&sig=" + channel.sig;
    }

    kodi::Log(ADDON_LOG_DEBUG, "Encrypted Stream URL -> %s", enc_url.c_str());

    SetStreamProperties(properties, enc_url, true, false, isLive/*, starttime, endtime*/);

    for (auto& prop : properties)
        kodi::Log(ADDON_LOG_DEBUG, "Name: %s Value: %s", prop.GetName().c_str(), prop.GetValue().c_str());

  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVREon::GetChannelStreamProperties(
    const kodi::addon::PVRChannel& channel, std::vector<kodi::addon::PVRStreamProperty>& properties)
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);
  EonChannel addonChannel;
  if (GetChannel(channel, addonChannel)) {
    if (addonChannel.subscribed) {
      return GetStreamProperties(addonChannel, properties, 0,/* 0,*/ true);
    }
    kodi::Log(ADDON_LOG_DEBUG, "Channel not subscribed");
    return PVR_ERROR_SERVER_ERROR;
  }
  kodi::Log(ADDON_LOG_DEBUG, "Channel not found");
  return PVR_ERROR_SERVER_ERROR;
}

PVR_ERROR CPVREon::GetChannelGroupsAmount(int& amount)
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);
  amount = static_cast<int>(m_categories.size());
  std::string amount_str = std::to_string(amount);
  kodi::Log(ADDON_LOG_DEBUG, "Groups Amount: [%s]", amount_str.c_str());

  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVREon::GetChannelGroups(bool bRadio, kodi::addon::PVRChannelGroupsResultSet& results)
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);

  std::vector<EonCategory>::iterator it;
  for (it = m_categories.begin(); it != m_categories.end(); ++it)
  {
    kodi::addon::PVRChannelGroup kodiGroup;

    if (bRadio == it->isRadio) {
      kodiGroup.SetPosition(it->order);
      kodiGroup.SetIsRadio(it->isRadio); /* is radio group */
      kodiGroup.SetGroupName(it->name);

      results.Add(kodiGroup);
      kodi::Log(ADDON_LOG_DEBUG, "Group added: %s at position %u", it->name.c_str(), it->order);
    }
  }

  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVREon::GetChannelGroupMembers(const kodi::addon::PVRChannelGroup& group,
                                           kodi::addon::PVRChannelGroupMembersResultSet& results)
{
  for (const auto& cgroup : m_categories)
  {
    if (cgroup.name != group.GetGroupName())
      continue;

    for (const auto& channel : cgroup.channels)
    {
      kodi::addon::PVRChannelGroupMember kodiGroupMember;

      kodiGroupMember.SetGroupName(group.GetGroupName());
      kodiGroupMember.SetChannelUniqueId(static_cast<unsigned int>(channel.id));
      kodiGroupMember.SetChannelNumber(static_cast<unsigned int>(channel.position));

      results.Add(kodiGroupMember);
    }
    return PVR_ERROR_NO_ERROR;
  }

  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVREon::GetSignalStatus(int channelUid, kodi::addon::PVRSignalStatus& signalStatus)
{
  signalStatus.SetAdapterName("pvr eon backend");
  signalStatus.SetAdapterStatus("OK");

  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVREon::GetRecordingsAmount(bool deleted, int& amount)
{
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVREon::GetRecordings(bool deleted, kodi::addon::PVRRecordingsResultSet& results)
{
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVREon::GetRecordingStreamProperties(
    const kodi::addon::PVRRecording& recording,
    std::vector<kodi::addon::PVRStreamProperty>& properties)
{
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVREon::GetTimerTypes(std::vector<kodi::addon::PVRTimerType>& types)
{
  /* TODO: Implement this to get support for the timer features introduced with PVR API 1.9.7 */
  return PVR_ERROR_NOT_IMPLEMENTED;
}

PVR_ERROR CPVREon::GetTimersAmount(int& amount)
{
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CPVREon::GetTimers(kodi::addon::PVRTimersResultSet& results)
{
  return PVR_ERROR_NO_ERROR;
}

bool CPVREon::GetChannel(const kodi::addon::PVRChannel& channel, EonChannel& myChannel)
{
  kodi::Log(ADDON_LOG_DEBUG, "function call: [%s]", __FUNCTION__);
  for (const auto& thisChannel : m_channels)
  {

    if (thisChannel.iUniqueId == (int)channel.GetUniqueId())
    {
      myChannel.iUniqueId = thisChannel.iUniqueId;
      myChannel.bRadio = thisChannel.bRadio;
      myChannel.bArchive = thisChannel.bArchive;
      myChannel.iChannelNumber = thisChannel.iChannelNumber;
//      myChannel.iSubChannelNumber = thisChannel.iSubChannelNumber;
//      myChannel.iEncryptionSystem = thisChannel.iEncryptionSystem;
//      myChannel.referenceID = thisChannel.referenceID;
      myChannel.strChannelName = thisChannel.strChannelName;
      myChannel.strIconPath = thisChannel.strIconPath;
      myChannel.publishingPoints = thisChannel.publishingPoints;
      myChannel.categories = thisChannel.categories;
//      mychannel.subtitleLanguage = thisChannel.subtitleLanguage;
      myChannel.sig = thisChannel.sig;
      myChannel.aaEnabled = thisChannel.aaEnabled;
      myChannel.subscribed = thisChannel.subscribed;
//      myChannel.profileIds = thisChannel.profileIds;
//      myChannel.strStreamURL = thisChannel.strStreamURL;

      return true;
    }
  }

  return false;
}

bool CPVREon::GetServer(bool isLive, EonServer& myServer)
{
  std::vector<EonServer> servers;
  if (isLive) {
    servers = m_live_servers;
  } else {
    servers = m_timeshift_servers;
  }
  int target_server = 2;
  if (m_platform == PLATFORM_ANDROIDTV) {
    target_server = 3;
  }
  int count = 0;
  for (const auto& thisServer : servers)
  {
      count++;
      if (count == target_server) {
        myServer.id = thisServer.id;
        myServer.ip = thisServer.ip;
        myServer.hostname = thisServer.hostname;
        return true;
      }
  }
  return false;
}

ADDONCREATOR(CPVREon)
