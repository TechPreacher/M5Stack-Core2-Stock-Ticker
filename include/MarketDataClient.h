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
  EmptySeries,
};

enum class MarketInterval { Hourly, Daily };

struct FetchResult {
  FetchStatus status = FetchStatus::NetworkError;
  MarketInterval interval = MarketInterval::Hourly;
  stock::Series series{};
  char message[80]{};
};

class MarketDataClient {
 public:
  FetchResult fetch(const char* symbol, const char* apiKey);

 private:
  FetchResult fetchInterval(const char* symbol, const char* apiKey,
                            MarketInterval interval) const;

  bool useDailyFallback_ = false;
};

FetchResult parseMarketPayload(char* payload, std::size_t payloadSize,
                               MarketInterval interval = MarketInterval::Hourly);