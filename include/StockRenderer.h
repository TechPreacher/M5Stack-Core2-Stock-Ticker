#pragma once

#include <M5Unified.h>

#include <cstddef>

#include "StockSeries.h"

class StockRenderer {
 public:
  StockRenderer();

  bool begin();
  void render(const char* symbol, const char* const* buttonSymbols,
              std::size_t buttonCount, std::size_t selectedButton,
              bool wifiConnected, const stock::Series* series,
              const char* status, bool stale);

 private:
  void drawGraph(const stock::Series& series, std::uint16_t color);
  void drawSymbolLabels(const char* const* symbols, std::size_t symbolCount,
                        std::size_t selectedSymbol);
  void drawWifiStatus(bool connected);

  M5Canvas canvas_;
  bool ready_ = false;
};