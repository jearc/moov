#include "moov.h"

void Chat::add_message(const Message &m)
{
  if (cursor != log.size())
    last_end_scroll_time = std::chrono::steady_clock::now();
  log.push_back(m);
  cursor = log.size();
}

std::span<Message> Chat::messages()
{
  if (log.size() == 0)
    return {};
  return { &log[0], cursor };
}

void Chat::scroll_up()
{
  if (cursor > 10)
    cursor--;
  if (cursor == log.size())
    last_end_scroll_time = std::chrono::steady_clock::now();
}

void Chat::scroll_down()
{
  if (cursor < log.size())
    cursor++;
  if (cursor == log.size())
    last_end_scroll_time = std::chrono::steady_clock::now();
}

time_point Chat::get_last_end_scroll_time()
{
  return last_end_scroll_time;
}
