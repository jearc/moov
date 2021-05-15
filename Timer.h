#pragma once
#include <SDL2/SDL.h>
#include <string>
#include <vector>

struct Timer
{
  // 'samples' is the number of start() -> stop() times to keep track of
  Timer(uint32_t samples);

  // begin a new sample
  void start();

  // begin a new sample at begin time t rather than now
  void start(uint64_t t);

  // stop and record time of current sample, does not resume the timer
  void stop();

  // stop recording and trash the sample we were recording
  void cancel();

  // resets the timer
  void clear_all();

  // manually inserts a time as if we did a start->stop sequence that resulted in this time
  void insert_time(double new_time);

  // returns the elapsed time of the current ongoing timer
  double get_current();

  // returns the time between the latest (start->stop)
  double get_last();

  // returns the average of all the samples taken
  double moving_average();
  double average();

  // returns the current number of samples taken so far
  uint32_t sample_count();

  // returns the maximum number of samples that are saved
  uint32_t max_sample_count();

  // returns the longest (start->stop) time out of all samples taken
  double longest();
  double moving_longest();

  // returns the shortest (start->stop) time out of all samples taken
  double shortest();
  double moving_shortest();

  // returns longest - shortest
  double jitter()
  {
    return longest() - shortest();
  }
  double moving_jitter()
  {
    return moving_longest() - moving_shortest();
  }

  // constructs a string providing all the above data
  std::string string_report();

  // get all of the active samples that have completed
  std::vector<double> get_times();

  // get all of the samples recorded sorted with latest at the front
  std::vector<double> get_ordered_times();

  // get the timestamp of the last call to start()
  uint64_t get_begin();

  // get the timestamp of the last call to stop() that was called while the
  // timer was running
  uint64_t get_end();

private:
  uint64_t freq;
  uint64_t begin;
  uint64_t end;
  bool stopped = true;
  double shortest_sample = std::numeric_limits<double>::infinity();
  double longest_sample = 0.0;
  double sum = 0;
  uint32_t sample_counter = 0;
  uint32_t max_valid_index = 0;
  uint32_t last_index = -1;
  uint32_t current_index = 0;
  std::vector<double> times;
};
