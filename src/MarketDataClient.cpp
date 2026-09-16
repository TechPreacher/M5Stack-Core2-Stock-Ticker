#include "MarketDataClient.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <esp_heap_caps.h>

#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>

#include "Certificates.h"

namespace {

constexpr std::size_t MaxPayloadBytes = 64U * 1024U;
constexpr std::uint32_t NetworkTimeoutMs = 12000;

void setMessage(FetchResult& result, const char* message) {
  std::snprintf(result.message, sizeof(result.message), "%s", message);
}

void setApiError(FetchResult& result, const char* apiMessage) {
  if (std::strstr(apiMessage, "We have detected your API key") != nullptr ||
      std::strstr(apiMessage, "API call frequency") != nullptr ||
      std::strstr(apiMessage, "rate limit") != nullptr) {
    result.status = FetchStatus::RateLimited;
    setMessage(result, "API daily limit");
  } else if (std::strstr(apiMessage, "premium endpoint") != nullptr) {
    result.status = FetchStatus::PremiumRequired;
    setMessage(result, "Premium required");
  } else if (std::strstr(apiMessage, "apikey") != nullptr ||
             std::strstr(apiMessage, "API key") != nullptr) {
    result.status = FetchStatus::ApiError;
    setMessage(result, "Invalid API key");
  } else {
    result.status = FetchStatus::ApiError;
    setMessage(result, "Market API error");
  }
}

String urlEncode(const char* value) {
  String encoded;
  while (*value != '\0') {
    const unsigned char character = static_cast<unsigned char>(*value++);
    if ((character >= 'a' && character <= 'z') ||
        (character >= 'A' && character <= 'Z') ||
        (character >= '0' && character <= '9') || character == '-' ||
        character == '_' || character == '.' || character == '~') {
      encoded += static_cast<char>(character);
    } else {
      char escaped[4]{};
      std::snprintf(escaped, sizeof(escaped), "%%%02X", character);
      encoded += escaped;
    }
  }
  return encoded;
}

bool readPayload(HTTPClient& http, char* payload, std::size_t& payloadSize) {
  WiFiClient* stream = http.getStreamPtr();
  const int contentLength = http.getSize();
  if (contentLength > static_cast<int>(MaxPayloadBytes)) {
    return false;
  }

  payloadSize = 0;
  std::uint32_t lastProgressAt = millis();
  while (http.connected() || stream->available() > 0) {
    const int available = stream->available();
    if (available <= 0) {
      if (millis() - lastProgressAt >= NetworkTimeoutMs) {
        return false;
      }
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }

    const std::size_t remaining = MaxPayloadBytes - payloadSize;
    if (remaining == 0) {
      return false;
    }
    const std::size_t bytesToRead =
        std::min<std::size_t>(static_cast<std::size_t>(available), remaining);
    const std::size_t bytesRead = stream->readBytes(payload + payloadSize, bytesToRead);
    if (bytesRead == 0) {
      return false;
    }
    payloadSize += bytesRead;
    lastProgressAt = millis();

    if (contentLength >= 0 && payloadSize >= static_cast<std::size_t>(contentLength)) {
      break;
    }
  }

  payload[payloadSize] = '\0';
  return payloadSize > 0;
}

bool parseClose(const char* text, float& value) {
  if (text == nullptr) {
    return false;
  }

  errno = 0;
  char* end = nullptr;
  value = std::strtof(text, &end);
  return errno == 0 && end != text && *end == '\0' && std::isfinite(value) &&
         value > 0.0F;
}

}  // namespace

MarketInterval preferredMarketInterval(stock::ChartPeriod period) {
  return period == stock::ChartPeriod::Monthly ? MarketInterval::Daily
                                                : MarketInterval::Hourly;
}

FetchResult MarketDataClient::fetch(const char* symbol, const char* apiKey,
                                    stock::ChartPeriod period) {
  const MarketInterval preferredInterval = preferredMarketInterval(period);
  if (useDailyFallback_ || preferredInterval == MarketInterval::Daily) {
    return fetchInterval(symbol, apiKey, MarketInterval::Daily, period);
  }

  FetchResult result =
      fetchInterval(symbol, apiKey, MarketInterval::Hourly, period);
  if (result.status == FetchStatus::PremiumRequired) {
    useDailyFallback_ = true;
    return fetchInterval(symbol, apiKey, MarketInterval::Daily, period);
  }
  return result;
}

FetchResult MarketDataClient::fetchInterval(const char* symbol, const char* apiKey,
                                            MarketInterval interval,
                                            stock::ChartPeriod period) const {
  FetchResult result{};
  result.interval = interval;
  result.period = period;
  WiFiClientSecure secureClient;
  secureClient.setCACert(certificates::GtsRootR4);
  secureClient.setTimeout(NetworkTimeoutMs / 1000U);

  String url = String("https://www.alphavantage.co/query?function=") +
               (interval == MarketInterval::Hourly ? "TIME_SERIES_INTRADAY"
                                                    : "TIME_SERIES_DAILY") +
               "&symbol=" + urlEncode(symbol) + "&outputsize=compact";
  if (interval == MarketInterval::Hourly) {
    url += "&interval=60min&extended_hours=false&entitlement=delayed";
  }
  url += "&datatype=json&apikey=" + urlEncode(apiKey);

  HTTPClient http;
  http.setConnectTimeout(NetworkTimeoutMs);
  http.setTimeout(NetworkTimeoutMs);
  if (!http.begin(secureClient, url)) {
    setMessage(result, "Could not initialize HTTPS");
    return result;
  }

  const int statusCode = http.GET();
  if (statusCode != HTTP_CODE_OK) {
    result.status = statusCode > 0 ? FetchStatus::HttpError : FetchStatus::NetworkError;
    std::snprintf(result.message, sizeof(result.message), "Request failed (%d)", statusCode);
    http.end();
    return result;
  }

  char* payload = static_cast<char*>(
      heap_caps_malloc(MaxPayloadBytes + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (payload == nullptr) {
    payload = static_cast<char*>(std::malloc(MaxPayloadBytes + 1));
  }
  if (payload == nullptr) {
    result.status = FetchStatus::ParseError;
    setMessage(result, "Not enough memory for response");
    http.end();
    return result;
  }

  std::size_t payloadSize = 0;
  if (!readPayload(http, payload, payloadSize)) {
    result.status = FetchStatus::PayloadTooLarge;
    setMessage(result, "Response exceeded limit or timed out");
    std::free(payload);
    http.end();
    return result;
  }
  http.end();

  result = parseMarketPayload(payload, payloadSize, interval, period);
  std::free(payload);
  return result;
}

FetchResult parseMarketPayload(char* payload, std::size_t payloadSize,
                               MarketInterval interval,
                               stock::ChartPeriod period) {
  FetchResult result{};
  result.interval = interval;
  result.period = period;
  JsonDocument document;
  const DeserializationError parseError = deserializeJson(document, payload, payloadSize);
  if (parseError) {
    result.status = FetchStatus::ParseError;
    setMessage(result, "Invalid market-data response");
    return result;
  }

  for (const char* key : {"Information", "Note", "Error Message"}) {
    const char* apiMessage = document[key];
    if (apiMessage != nullptr) {
      setApiError(result, apiMessage);
      return result;
    }
  }

  const char* seriesKey = interval == MarketInterval::Hourly ? "Time Series (60min)"
                                                              : "Time Series (Daily)";
  JsonObject timeSeries = document[seriesKey].as<JsonObject>();
  for (JsonPair entry : timeSeries) {
    float close = 0.0F;
    if (parseClose(entry.value()["4. close"], close)) {
      stock::addPoint(result.series, entry.key().c_str(), close);
    }
  }

  std::size_t tradingDays = stock::tradingDaysForPeriod(period);
  if (interval == MarketInterval::Daily &&
      period == stock::ChartPeriod::Daily) {
    tradingDays = 2;
  }
  stock::sortAndKeepLatestTradingDays(result.series, tradingDays);
  if (result.series.count == 0) {
    result.status = FetchStatus::EmptySeries;
    setMessage(result, "No valid prices returned");
    return result;
  }

  result.status = FetchStatus::Success;
  setMessage(result, "Updated");
  return result;
}