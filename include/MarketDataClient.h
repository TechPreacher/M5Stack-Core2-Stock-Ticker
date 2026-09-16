#pragma once

#include <cstddef>

#include "StockSeries.h"

enum class FetchStatus {
  Success,
  NetworkError,
  HttpError,
  PayloadTooLarge,
  ParseError,
  ApiError,
  RateLimited,
  PremiumRequired,
  EmptySeries,
};

enum class MarketInterval { Hourly, Daily };

struct FetchResult {
  FetchStatus status = FetchStatus::NetworkError;
  MarketInterval interval = MarketInterval::Hourly;
  stock::ChartPeriod period = stock::ChartPeriod::Daily;
  stock::Series series{};
  char message[80]{};
};

class MarketDataClient {
 public:
  FetchResult fetch(const char* symbol, const char* apiKey,
                    stock::ChartPeriod period);

 private:
  FetchResult fetchInterval(const char* symbol, const char* apiKey,
                            MarketInterval interval,
                            stock::ChartPeriod period) const;

  bool useDailyFallback_ = false;
};

MarketInterval preferredMarketInterval(stock::ChartPeriod period);
FetchResult parseMarketPayload(char* payload, std::size_t payloadSize,
                               MarketInterval interval = MarketInterval::Hourly,
                               stock::ChartPeriod period =
                   stock::ChartPeriod::Daily);