/**
 * @file scheduled_sms.h
 * @brief Scheduled SMS runtime owner and task domain model.
 */

#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

class ScheduledStore;

constexpr size_t kMaxScheduledTasks = 16;
constexpr size_t kScheduledTaskPreviewMaxBytes = 96;
constexpr size_t kScheduledTaskBodyMaxBytes = 12288;

enum class ScheduledIntervalUnit : uint8_t {
  Minutes = 0,
  Hours = 1,
  Days = 2,
  Weeks = 3,
  Months = 4,
};

enum class ScheduledEndPolicy : uint8_t {
  Never = 0,
  OnDate = 1,
  AfterRuns = 2,
};

struct ScheduledTaskRecord {
  uint32_t id = 0;
  bool enabled = true;
  String name;
  String phone;
  String preview;
  size_t body_bytes = 0;
  time_t first_run_utc = 0;
  bool repeat_enabled = false;
  uint32_t repeat_every = 0;
  ScheduledIntervalUnit repeat_unit = ScheduledIntervalUnit::Days;
  ScheduledEndPolicy end_policy = ScheduledEndPolicy::Never;
  time_t end_at_utc = 0;
  uint32_t max_runs = 0;
  uint32_t run_count = 0;
  time_t next_run_utc = 0;
  time_t last_run_utc = 0;
  bool last_run_success = false;
  String last_result;
};

struct ScheduledTaskDraft {
  uint32_t id = 0;
  bool enabled = true;
  String name;
  String phone;
  String body;
  time_t first_run_utc = 0;
  bool repeat_enabled = false;
  uint32_t repeat_every = 0;
  ScheduledIntervalUnit repeat_unit = ScheduledIntervalUnit::Days;
  ScheduledEndPolicy end_policy = ScheduledEndPolicy::Never;
  time_t end_at_utc = 0;
  uint32_t max_runs = 0;
};

struct ScheduledTaskDispatch {
  uint32_t task_id = 0;
  String phone;
};

class ScheduledSms {
 public:
  explicit ScheduledSms(ScheduledStore& store);

  bool Begin(String& message);

  size_t CopyTasks(ScheduledTaskRecord* tasks, size_t capacity) const;
  bool LoadTask(uint32_t task_id, ScheduledTaskDraft& draft, String& message) const;

  bool UpsertTask(const ScheduledTaskDraft& draft, String& message, uint32_t* saved_task_id);
  bool DeleteTask(uint32_t task_id, String& message);
  bool SetTaskEnabled(uint32_t task_id, bool enabled, String& message);

  bool PrepareManualRun(uint32_t task_id, ScheduledTaskDispatch& dispatch,
                        String& message);
  void CompleteManualRun(uint32_t task_id, time_t executed_at, bool success,
                         const String& result_message);

  bool PrepareDueRun(time_t now_utc, ScheduledTaskDispatch& dispatch);
  void CompleteDueRun(uint32_t task_id, time_t executed_at, bool success,
                      const String& result_message);
  time_t GetNextDueUtc() const;

 private:
  int FindTaskIndexLocked(uint32_t task_id) const;
  bool ValidateDraftLocked(const ScheduledTaskDraft& draft, String& message) const;
  ScheduledTaskRecord BuildRecordLocked(const ScheduledTaskDraft& draft,
                                        const ScheduledTaskRecord* existing) const;
  time_t ComputeInitialNextRunUtcLocked(const ScheduledTaskDraft& draft,
                                        time_t now_utc) const;
  time_t ComputeNextRunAfterLocked(const ScheduledTaskRecord& task,
                                   time_t after_utc) const;
  bool HasReachedEndLocked(const ScheduledTaskRecord& task, time_t candidate_utc) const;
  String BuildPreviewLocked(const String& body) const;
  void SortTasksLocked();
  void FinishRunLocked(ScheduledTaskRecord& task, bool count_run, time_t executed_at,
                       bool success, const String& result_message);

  ScheduledStore& store_;
  SemaphoreHandle_t mutex_;
  ScheduledTaskRecord tasks_[kMaxScheduledTasks];
  bool dispatching_[kMaxScheduledTasks];
  size_t task_count_;
  uint32_t next_task_id_;
};
