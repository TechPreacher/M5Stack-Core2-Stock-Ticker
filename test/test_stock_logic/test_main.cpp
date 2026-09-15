#include <unity.h>

#include <cmath>
#include <cstring>

#include "MarketDataClient.h"
#include "StockSeries.h"

void setUp() {}
void tearDown() {}

void test_given_six_trading_days_when_normalized_then_keeps_latest_five() {
  stock::Series series{};
  stock::addPoint(series, "2026-09-14 10:30:00", 106.0F);
  stock::addPoint(series, "2026-09-08 10:30:00", 102.0F);
  stock::addPoint(series, "2026-09-07 10:30:00", 101.0F);
  stock::addPoint(series, "2026-09-11 10:30:00", 105.0F);
  stock::addPoint(series, "2026-09-09 10:30:00", 103.0F);
  stock::addPoint(series, "2026-09-10 10:30:00", 104.0F);

  stock::sortAndKeepLatestTradingDays(series, 5);

  TEST_ASSERT_EQUAL_UINT32(5, series.count);
  TEST_ASSERT_EQUAL_STRING("2026-09-08 10:30:00", series.points[0].timestamp);
  TEST_ASSERT_EQUAL_STRING("2026-09-14 10:30:00",
                           series.points[series.count - 1].timestamp);
}

void test_given_flat_prices_when_range_calculated_then_span_is_nonzero() {
  stock::Series series{};
  stock::addPoint(series, "2026-09-14 10:30:00", 420.0F);
  stock::addPoint(series, "2026-09-14 11:30:00", 420.0F);

  const stock::PriceRange range = stock::calculatePriceRange(series);

  TEST_ASSERT_TRUE(range.minimum < 420.0F);
  TEST_ASSERT_TRUE(range.maximum > 420.0F);
  TEST_ASSERT_EQUAL(stock::Trend::Flat, stock::calculateTrend(series));
}

void test_given_valid_payload_when_parsed_then_orders_prices() {
  char payload[] =
      R"json({"Time Series (60min)":{"2026-09-14 11:30:00":{"4. close":"421.25"},"2026-09-14 10:30:00":{"4. close":"420.50"}}})json";

  const FetchResult result = parseMarketPayload(payload, std::strlen(payload));

  TEST_ASSERT_EQUAL(FetchStatus::Success, result.status);
  TEST_ASSERT_EQUAL_UINT32(2, result.series.count);
  TEST_ASSERT_FLOAT_WITHIN(0.001F, 420.50F, result.series.points[0].close);
  TEST_ASSERT_EQUAL(stock::Trend::Up, stock::calculateTrend(result.series));
}

void test_given_api_error_when_parsed_then_returns_api_error() {
  char payload[] = R"json({"Information":"premium endpoint"})json";

  const FetchResult result = parseMarketPayload(payload, std::strlen(payload));

  TEST_ASSERT_EQUAL(FetchStatus::ApiError, result.status);
  TEST_ASSERT_EQUAL_STRING("premium endpoint", result.message);
}

void test_given_daily_payload_when_parsed_then_accepts_date_timestamps() {
  char payload[] =
      R"json({"Time Series (Daily)":{"2026-09-14":{"4. close":"421.25"},"2026-09-11":{"4. close":"420.50"}}})json";

  const FetchResult result =
      parseMarketPayload(payload, std::strlen(payload), MarketInterval::Daily);

  TEST_ASSERT_EQUAL(FetchStatus::Success, result.status);
  TEST_ASSERT_EQUAL(MarketInterval::Daily, result.interval);
  TEST_ASSERT_EQUAL_UINT32(2, result.series.count);
  TEST_ASSERT_EQUAL_STRING("2026-09-11", result.series.points[0].timestamp);
}

void test_given_malformed_payload_when_parsed_then_returns_parse_error() {
  char payload[] = "{not-json";

  const FetchResult result = parseMarketPayload(payload, std::strlen(payload));

  TEST_ASSERT_EQUAL(FetchStatus::ParseError, result.status);
}

void setup() {
  UNITY_BEGIN();
  RUN_TEST(test_given_six_trading_days_when_normalized_then_keeps_latest_five);
  RUN_TEST(test_given_flat_prices_when_range_calculated_then_span_is_nonzero);
  RUN_TEST(test_given_valid_payload_when_parsed_then_orders_prices);
  RUN_TEST(test_given_api_error_when_parsed_then_returns_api_error);
  RUN_TEST(test_given_daily_payload_when_parsed_then_accepts_date_timestamps);
  RUN_TEST(test_given_malformed_payload_when_parsed_then_returns_parse_error);
  UNITY_END();
}

void loop() {}