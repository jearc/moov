#include "Timer.h"
#include <algorithm>
template <typename T> void ASSERT(T t)
{
#ifndef DISABLE_ASSERT
  if (!t)
  {
    throw;
  }
#endif
}

Timer::Timer(uint32_t samples)
{
  times = std::vector<double>(samples, -1.);
  freq = SDL_GetPerformanceFrequency();
}

void Timer::start()
{
  if (stopped)
    begin = SDL_GetPerformanceCounter();
  stopped = false;
}

void Timer::start(uint64_t t)
{
  ASSERT(t > 0);
  stopped = false;
  begin = t;
}

void Timer::stop()
{
  if (!stopped)
  {
    end = SDL_GetPerformanceCounter();
    stopped = true;
    ASSERT(end >= begin);
    double new_time = (double)(end - begin) / freq;
    times[current_index] = new_time;
    shortest_sample = std::min(shortest_sample,new_time);
    longest_sample = std::max(longest_sample,new_time);
    sample_counter += 1 ;
    last_index = current_index;
    ++current_index;
    if (max_valid_index < times.size())
      ++max_valid_index;
    if (current_index > times.size() - 1)
      current_index = 0;
  }
}

void Timer::cancel()
{
  stopped = true;
}

void Timer::clear_all()
{
  stopped = true;
  max_valid_index = 0;
  current_index = 0;
  last_index = -1;  
  shortest_sample = std::numeric_limits<double>::infinity();
  longest_sample = 0.0;
  begin = 0;
  end = 0;
  times = std::vector<double>(times.size(), -1.);
}

void Timer::insert_time(double new_time)
{
  times[current_index] = new_time;
  shortest_sample = std::min(shortest_sample, new_time);
  longest_sample = std::max(longest_sample, new_time);
  sample_counter += 1;
  last_index = current_index;
  ++current_index;
  if (max_valid_index < times.size())
    ++max_valid_index;
  if (current_index > times.size() - 1)
    current_index = 0;
}

// returns the elapsed time of the current ongoing timer

double Timer::get_current()
{
  if (stopped)
    return 0.;
  double now = SDL_GetPerformanceCounter();
  return (double)(now - begin) / freq;
}

double Timer::get_last()
{
  if (last_index != -1)
    return times[last_index];
  else
    return -1;
}

double Timer::moving_average()
{
  double sum = 0;
  uint32_t count = 0;
  for (uint32_t i = 0; i < max_valid_index; ++i)
  {
    sum += times[i];
    count += 1;
  }
  return sum / count;
}
double Timer::average()
{
  return sum / sample_counter;
}

uint32_t Timer::sample_count()
{
  return sample_counter;
}

uint32_t Timer::max_sample_count()
{
  return times.size();
}

double Timer::longest()
{
  return longest_sample;
}

double Timer::shortest()
{
  return shortest_sample;
}

double Timer::moving_longest()
{
  return *std::max_element(times.begin(),times.end());
}
double Timer::moving_shortest()
{
  return *std::min_element(times.begin(), times.end());
}

std::string Timer::string_report()
{
  std::string s = "";
  s += "Last          : " + std::to_string(get_last()) + "\n";
  s += "Moving average       : " + std::to_string(moving_average()) + "\n";
  s += "Moving Max           : " + std::to_string(moving_longest()) + "\n";
  s += "Moving Min           : " + std::to_string(moving_shortest()) + "\n";
  s += "Moving Jitter        : " + std::to_string(moving_jitter()) + "\n";
  s += "Moving samples:      : " + std::to_string(max_valid_index) + "\n";
  s += "Total average       : " + std::to_string(average()) + "\n";
  s += "Total Max           : " + std::to_string(longest()) + "\n";
  s += "Total Min           : " + std::to_string(shortest()) + "\n";
  s += "Total Jitter        : " + std::to_string(jitter()) + "\n";
  s += "Total samples:      : " + std::to_string(sample_counter) + "\n";
  return s;
}

std::vector<double> Timer::get_times()
{
  std::vector<double> result;
  result.reserve(max_valid_index);
  for (auto &t : times)
  {
    if (t != -1)
      result.push_back(t);
  }
  return result;
}

// get all of the samples recorded sorted with latest at the front

std::vector<double> Timer::get_ordered_times()
{
  std::vector<double> result;
  for (int32_t i = 0; i < max_valid_index; ++i)
  {
    int32_t index = (int32_t)current_index - i;

    if (index < 0)
    {
      index = times.size() - abs(index);
    }

    result.push_back(times[index]);
  }
  return result;
}

uint64_t Timer::get_begin()
{
  return begin;
}
uint64_t Timer::get_end()
{
  return end;
}
