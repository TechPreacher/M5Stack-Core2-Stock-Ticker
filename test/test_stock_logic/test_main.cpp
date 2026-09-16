#include <unity.h>

#include <cmath>
#include <cstring>

#include "AppSettings.h"
#include "MarketDataClient.h"
#include "StockSeries.h"

void setUp() {}
void tearDown() {}

void test_given_chart_period_when_advanced_then_cycles_daily_weekly_monthly() {
  TEST_ASSERT_EQUAL(stock::ChartPeriod::Weekly,
                    stock::nextChartPeriod(stock::ChartPeriod::Daily));
  TEST_ASSERT_EQUAL(stock::ChartPeriod::Monthly,
                    stock::nextChartPeriod(stock::ChartPeriod::Weekly));
  TEST_ASSERT_EQUAL(stock::ChartPeriod::Daily,
                    stock::nextChartPeriod(stock::ChartPeriod::Monthly));
  TEST_ASSERT_EQUAL_UINT32(
      1, stock::tradingDaysForPeriod(stock::ChartPeriod::Daily));
  TEST_ASSERT_EQUAL_UINT32(
      5, stock::tradingDaysForPeriod(stock::ChartPeriod::Weekly));
  TEST_ASSERT_EQUAL_UINT32(
      22, stock::tradingDaysForPeriod(stock::ChartPeriod::Monthly));
}

void test_given_symbol_press_sequence_when_period_selected_then_resets_or_advances() {
  TEST_ASSERT_EQUAL(
      stock::ChartPeriod::Daily,
    stock::chartPeriodAfterPress(stock::ChartPeriod::Monthly, false));
  TEST_ASSERT_EQUAL(
      stock::ChartPeriod::Weekly,
    stock::chartPeriodAfterPress(stock::ChartPeriod::Daily, true));
  TEST_ASSERT_EQUAL(
      stock::ChartPeriod::Monthly,
    stock::chartPeriodAfterPress(stock::ChartPeriod::Weekly, true));
  TEST_ASSERT_EQUAL(
    stock::ChartPeriod::Daily,
    stock::chartPeriodAfterPress(stock::ChartPeriod::Monthly, true));
}

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

void test_given_premium_response_when_parsed_then_preserves_entitlement_status() {
  char payload[] =
      R"json({"Information":"This is a long provider response whose important classification appears after the display buffer and identifies a premium endpoint"})json";

  const FetchResult result = parseMarketPayload(payload, std::strlen(payload));

  TEST_ASSERT_EQUAL(FetchStatus::PremiumRequired, result.status);
  TEST_ASSERT_EQUAL_STRING("Premium required", result.message);
}

void test_given_daily_limit_response_when_parsed_then_returns_short_status() {
  char payload[] =
      R"json({"Information":"We have detected your API key as REDACTED and our standard API rate limit is 25 requests per day."})json";

  const FetchResult result = parseMarketPayload(payload, std::strlen(payload));

  TEST_ASSERT_EQUAL(FetchStatus::RateLimited, result.status);
  TEST_ASSERT_EQUAL_STRING("API daily limit", result.message);
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

void test_given_daily_chart_with_daily_data_when_parsed_then_keeps_two_closes() {
  char payload[] =
      R"json({"Time Series (Daily)":{"2026-09-14":{"4. close":"421.25"},"2026-09-11":{"4. close":"420.50"},"2026-09-10":{"4. close":"419.75"}}})json";

  const FetchResult result =
      parseMarketPayload(payload, std::strlen(payload), MarketInterval::Daily,
                         stock::ChartPeriod::Daily);

  TEST_ASSERT_EQUAL(FetchStatus::Success, result.status);
  TEST_ASSERT_EQUAL(stock::ChartPeriod::Daily, result.period);
  TEST_ASSERT_EQUAL_UINT32(2, result.series.count);
  TEST_ASSERT_EQUAL_STRING("2026-09-11", result.series.points[0].timestamp);
  TEST_ASSERT_EQUAL_STRING("2026-09-14", result.series.points[1].timestamp);
}

void test_given_chart_period_when_interval_selected_then_monthly_uses_daily() {
  TEST_ASSERT_EQUAL(MarketInterval::Hourly,
                    preferredMarketInterval(stock::ChartPeriod::Daily));
  TEST_ASSERT_EQUAL(MarketInterval::Hourly,
                    preferredMarketInterval(stock::ChartPeriod::Weekly));
  TEST_ASSERT_EQUAL(MarketInterval::Daily,
                    preferredMarketInterval(stock::ChartPeriod::Monthly));
}

void test_given_malformed_payload_when_parsed_then_returns_parse_error() {
  char payload[] = "{not-json";

  const FetchResult result = parseMarketPayload(payload, std::strlen(payload));

  TEST_ASSERT_EQUAL(FetchStatus::ParseError, result.status);
}

void test_given_valid_ini_when_parsed_then_loads_settings() {
  constexpr char input[] =
      "[wifi]\nssid = Test Network\npassword = secret\n\n"
      "[market]\napi_key = demo\nsymbol_a = msft\nsymbol_b = AAPL\n"
      "symbol_c = brk-b\nrefresh_minutes = 45\n";
  AppSettings settings{};

  const SettingsParseResult result =
      parseAppSettings(input, sizeof(input) - 1, settings);

  TEST_ASSERT_EQUAL(SettingsParseStatus::Success, result.status);
  TEST_ASSERT_EQUAL_STRING("Test Network", settings.wifiSsid);
  TEST_ASSERT_EQUAL_STRING("MSFT", settings.symbols[0]);
  TEST_ASSERT_EQUAL_STRING("BRK-B", settings.symbols[2]);
  TEST_ASSERT_EQUAL_UINT32(45UL * 60UL * 1000UL, settings.refreshIntervalMs);
}

void test_given_missing_ini_key_when_parsed_then_returns_error() {
  constexpr char input[] = "[wifi]\nssid = Test Network\n";
  AppSettings settings{};

  const SettingsParseResult result =
      parseAppSettings(input, sizeof(input) - 1, settings);

  TEST_ASSERT_EQUAL(SettingsParseStatus::Invalid, result.status);
  TEST_ASSERT_EQUAL_STRING("settings.ini incomplete", result.message);
}

void test_given_invalid_symbol_when_parsed_then_returns_error() {
  constexpr char input[] =
      "[wifi]\nssid=x\npassword=y\n[market]\napi_key=z\n"
      "symbol_a=BAD SYMBOL\nsymbol_b=AAPL\nsymbol_c=GOOGL\n"
      "refresh_minutes=30\n";
  AppSettings settings{};

  const SettingsParseResult result =
      parseAppSettings(input, sizeof(input) - 1, settings);

  TEST_ASSERT_EQUAL(SettingsParseStatus::Invalid, result.status);
  TEST_ASSERT_EQUAL_STRING("Invalid stock symbol", result.message);
}

void setup() {
  UNITY_BEGIN();
  RUN_TEST(test_given_chart_period_when_advanced_then_cycles_daily_weekly_monthly);
  RUN_TEST(
      test_given_symbol_press_sequence_when_period_selected_then_resets_or_advances);
  RUN_TEST(test_given_six_trading_days_when_normalized_then_keeps_latest_five);
  RUN_TEST(test_given_flat_prices_when_range_calculated_then_span_is_nonzero);
  RUN_TEST(test_given_valid_payload_when_parsed_then_orders_prices);
    RUN_TEST(
      test_given_premium_response_when_parsed_then_preserves_entitlement_status);
    RUN_TEST(
      test_given_daily_limit_response_when_parsed_then_returns_short_status);
  RUN_TEST(test_given_daily_payload_when_parsed_then_accepts_date_timestamps);
    RUN_TEST(
      test_given_daily_chart_with_daily_data_when_parsed_then_keeps_two_closes);
    RUN_TEST(
      test_given_chart_period_when_interval_selected_then_monthly_uses_daily);
  RUN_TEST(test_given_malformed_payload_when_parsed_then_returns_parse_error);
  RUN_TEST(test_given_valid_ini_when_parsed_then_loads_settings);
  RUN_TEST(test_given_missing_ini_key_when_parsed_then_returns_error);
  RUN_TEST(test_given_invalid_symbol_when_parsed_then_returns_error);
  UNITY_END();
}

void loop() {}