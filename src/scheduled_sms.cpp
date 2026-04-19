/**
 * @file scheduled_sms.cpp
 * @brief Scheduled SMS runtime owner and task domain model.
 */

#include "scheduled_sms.h"

#include <time.h>

#include <algorithm>

#include "modem.h"
#include "scheduled_store.h"

namespace {

constexpr time_t kClockValidThreshold = 100000;

bool IsClockValid(time_t now_utc) {
  return now_utc >= kClockValidThreshold;
}

bool IsLeapYear(int year) {
  return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

int DaysInMonth(int year, int month_index) {
  static const int kMonthDays[] = {31, 28, 31, 30, 31, 30,
                                   31, 31, 30, 31, 30, 31};
  if (month_index == 1 && IsLeapYear(year)) {
    return 29;
  }
  return kMonthDays[month_index];
}

time_t AddMonthsLocalCalendar(time_t base_utc, uint32_t months) {
  struct tm time_parts;
#if defined(_WIN32)
  localtime_s(&time_parts, &base_utc);
#else
  localtime_r(&base_utc, &time_parts);
#endif
  const int original_day = time_parts.tm_mday;
  time_parts.tm_mday = 1;
  time_parts.tm_mon += static_cast<int>(months);
  mktime(&time_parts);
  const int month_last_day =
      DaysInMonth(time_parts.tm_year + 1900, time_parts.tm_mon);
  time_parts.tm_mday = std::min(original_day, month_last_day);
  return mktime(&time_parts);
}

time_t AddIntervalUtc(time_t base_utc, uint32_t every, ScheduledIntervalUnit unit) {
  if (every == 0) {
    return base_utc;
  }

  switch (unit) {
    case ScheduledIntervalUnit::Minutes:
      return base_utc + static_cast<time_t>(every) * 60;
    case ScheduledIntervalUnit::Hours:
      return base_utc + static_cast<time_t>(every) * 60 * 60;
    case ScheduledIntervalUnit::Days:
      return base_utc + static_cast<time_t>(every) * 60 * 60 * 24;
    case ScheduledIntervalUnit::Weeks:
      return base_utc + static_cast<time_t>(every) * 60 * 60 * 24 * 7;
    case ScheduledIntervalUnit::Months:
      return AddMonthsLocalCalendar(base_utc, every);
  }

  return base_utc;
}

}  // namespace

ScheduledSms::ScheduledSms(ScheduledStore& store)
    : store_(store),
      mutex_(xSemaphoreCreateMutex()),
      task_count_(0),
      next_task_id_(1) {
  for (size_t index = 0; index < kMaxScheduledTasks; ++index) {
    dispatching_[index] = false;
  }
}

bool ScheduledSms::Begin(String& message) {
  if (xSemaphoreTake(mutex_, portMAX_DELAY) != pdTRUE) {
    message = "定时任务运行时忙，请稍后重试。";
    return false;
  }

  task_count_ = 0;
  next_task_id_ = 1;
  for (size_t index = 0; index < kMaxScheduledTasks; ++index) {
    dispatching_[index] = false;
  }

  const bool success = store_.Load(tasks_, kMaxScheduledTasks, task_count_, next_task_id_,
                                   message);
  if (success) {
    SortTasksLocked();
  }

  xSemaphoreGive(mutex_);
  return success;
}

size_t ScheduledSms::CopyTasks(ScheduledTaskRecord* tasks, size_t capacity) const {
  if (tasks == nullptr || capacity == 0) {
    return 0;
  }

  if (xSemaphoreTake(mutex_, portMAX_DELAY) != pdTRUE) {
    return 0;
  }

  const size_t count = task_count_ < capacity ? task_count_ : capacity;
  for (size_t index = 0; index < count; ++index) {
    tasks[index] = tasks_[index];
  }

  xSemaphoreGive(mutex_);
  return count;
}

bool ScheduledSms::LoadTask(uint32_t task_id, ScheduledTaskDraft& draft, String& message) const {
  if (xSemaphoreTake(mutex_, portMAX_DELAY) != pdTRUE) {
    message = "定时任务运行时忙，请稍后重试。";
    return false;
  }

  const int index = FindTaskIndexLocked(task_id);
  if (index < 0) {
    xSemaphoreGive(mutex_);
    message = "未找到定时任务。";
    return false;
  }

  const ScheduledTaskRecord& task = tasks_[index];
  draft.id = task.id;
  draft.enabled = task.enabled;
  draft.name = task.name;
  draft.phone = task.phone;
  draft.first_run_utc = task.first_run_utc;
  draft.repeat_enabled = task.repeat_enabled;
  draft.repeat_every = task.repeat_every;
  draft.repeat_unit = task.repeat_unit;
  draft.end_policy = task.end_policy;
  draft.end_at_utc = task.end_at_utc;
  draft.max_runs = task.max_runs;
  xSemaphoreGive(mutex_);

  return store_.LoadTaskBody(task_id, draft.body, message);
}

bool ScheduledSms::UpsertTask(const ScheduledTaskDraft& draft, String& message,
                              uint32_t* saved_task_id) {
  if (xSemaphoreTake(mutex_, portMAX_DELAY) != pdTRUE) {
    message = "定时任务运行时忙，请稍后重试。";
    return false;
  }

  if (!ValidateDraftLocked(draft, message)) {
    xSemaphoreGive(mutex_);
    return false;
  }

  const int existing_index = draft.id == 0 ? -1 : FindTaskIndexLocked(draft.id);
  if (draft.id != 0 && existing_index < 0) {
    xSemaphoreGive(mutex_);
    message = "未找到要更新的定时任务。";
    return false;
  }
  if (existing_index < 0 && task_count_ >= kMaxScheduledTasks) {
    xSemaphoreGive(mutex_);
    message = "定时任务数量已达上限。";
    return false;
  }

  ScheduledTaskRecord record =
      BuildRecordLocked(draft, existing_index >= 0 ? &tasks_[existing_index] : nullptr);
  if (!store_.StoreTask(record, draft.body, message)) {
    xSemaphoreGive(mutex_);
    return false;
  }

  if (existing_index >= 0) {
    tasks_[existing_index] = record;
  } else {
    tasks_[task_count_++] = record;
    next_task_id_ = record.id + 1;
  }

  SortTasksLocked();
  xSemaphoreGive(mutex_);

  if (saved_task_id != nullptr) {
    *saved_task_id = record.id;
  }
  message = existing_index >= 0 ? "定时任务已更新。" : "定时任务已创建。";
  return true;
}

bool ScheduledSms::DeleteTask(uint32_t task_id, String& message) {
  if (xSemaphoreTake(mutex_, portMAX_DELAY) != pdTRUE) {
    message = "定时任务运行时忙，请稍后重试。";
    return false;
  }

  const int index = FindTaskIndexLocked(task_id);
  if (index < 0) {
    xSemaphoreGive(mutex_);
    message = "未找到定时任务。";
    return false;
  }

  if (!store_.DeleteTask(task_id, message)) {
    xSemaphoreGive(mutex_);
    return false;
  }

  for (size_t offset = static_cast<size_t>(index) + 1; offset < task_count_; ++offset) {
    tasks_[offset - 1] = tasks_[offset];
    dispatching_[offset - 1] = dispatching_[offset];
  }
  if (task_count_ > 0) {
    --task_count_;
    dispatching_[task_count_] = false;
    tasks_[task_count_] = ScheduledTaskRecord{};
  }

  xSemaphoreGive(mutex_);
  message = "定时任务已删除。";
  return true;
}

bool ScheduledSms::SetTaskEnabled(uint32_t task_id, bool enabled, String& message) {
  if (xSemaphoreTake(mutex_, portMAX_DELAY) != pdTRUE) {
    message = "定时任务运行时忙，请稍后重试。";
    return false;
  }

  const int index = FindTaskIndexLocked(task_id);
  if (index < 0) {
    xSemaphoreGive(mutex_);
    message = "未找到定时任务。";
    return false;
  }

  ScheduledTaskRecord updated = tasks_[index];
  updated.enabled = enabled;
  if (!enabled) {
    updated.next_run_utc = 0;
  } else if (updated.repeat_enabled) {
    const time_t now_utc = time(nullptr);
    updated.next_run_utc = ComputeNextRunAfterLocked(updated, now_utc);
  } else {
    updated.next_run_utc = updated.run_count == 0 ? updated.first_run_utc : 0;
  }

  if (!store_.StoreTaskRecord(updated, message)) {
    xSemaphoreGive(mutex_);
    return false;
  }

  tasks_[index] = updated;
  SortTasksLocked();
  xSemaphoreGive(mutex_);
  message = enabled ? "定时任务已启用。" : "定时任务已暂停。";
  return true;
}

bool ScheduledSms::PrepareManualRun(uint32_t task_id, ScheduledTaskDispatch& dispatch,
                                    String& message) {
  if (xSemaphoreTake(mutex_, portMAX_DELAY) != pdTRUE) {
    message = "定时任务运行时忙，请稍后重试。";
    return false;
  }

  const int index = FindTaskIndexLocked(task_id);
  if (index < 0) {
    xSemaphoreGive(mutex_);
    message = "未找到定时任务。";
    return false;
  }
  if (dispatching_[index]) {
    xSemaphoreGive(mutex_);
    message = "定时任务正在执行，请稍后再试。";
    return false;
  }

  dispatching_[index] = true;
  dispatch.task_id = tasks_[index].id;
  dispatch.phone = tasks_[index].phone;
  xSemaphoreGive(mutex_);
  message = "";
  return true;
}

void ScheduledSms::CompleteManualRun(uint32_t task_id, time_t executed_at, bool success,
                                     const String& result_message) {
  if (xSemaphoreTake(mutex_, portMAX_DELAY) != pdTRUE) {
    return;
  }

  const int index = FindTaskIndexLocked(task_id);
  if (index >= 0) {
    dispatching_[index] = false;
    FinishRunLocked(tasks_[index], false, executed_at, success, result_message);
    String ignored_message;
    store_.StoreTaskRecord(tasks_[index], ignored_message);
  }

  xSemaphoreGive(mutex_);
}

bool ScheduledSms::PrepareDueRun(time_t now_utc, ScheduledTaskDispatch& dispatch) {
  if (!IsClockValid(now_utc)) {
    return false;
  }

  if (xSemaphoreTake(mutex_, portMAX_DELAY) != pdTRUE) {
    return false;
  }

  int selected_index = -1;
  time_t selected_next_run = 0;
  for (size_t index = 0; index < task_count_; ++index) {
    const ScheduledTaskRecord& task = tasks_[index];
    if (!task.enabled || dispatching_[index] || task.next_run_utc == 0 ||
        task.next_run_utc > now_utc) {
      continue;
    }
    if (selected_index < 0 || task.next_run_utc < selected_next_run) {
      selected_index = static_cast<int>(index);
      selected_next_run = task.next_run_utc;
    }
  }

  if (selected_index < 0) {
    xSemaphoreGive(mutex_);
    return false;
  }

  dispatching_[selected_index] = true;
  dispatch.task_id = tasks_[selected_index].id;
  dispatch.phone = tasks_[selected_index].phone;
  xSemaphoreGive(mutex_);
  return true;
}

void ScheduledSms::CompleteDueRun(uint32_t task_id, time_t executed_at, bool success,
                                  const String& result_message) {
  if (xSemaphoreTake(mutex_, portMAX_DELAY) != pdTRUE) {
    return;
  }

  const int index = FindTaskIndexLocked(task_id);
  if (index >= 0) {
    dispatching_[index] = false;
    FinishRunLocked(tasks_[index], true, executed_at, success, result_message);
    String ignored_message;
    store_.StoreTaskRecord(tasks_[index], ignored_message);
    SortTasksLocked();
  }

  xSemaphoreGive(mutex_);
}

time_t ScheduledSms::GetNextDueUtc() const {
  if (xSemaphoreTake(mutex_, portMAX_DELAY) != pdTRUE) {
    return 0;
  }

  time_t next_due_utc = 0;
  for (size_t index = 0; index < task_count_; ++index) {
    const ScheduledTaskRecord& task = tasks_[index];
    if (!task.enabled || task.next_run_utc == 0 || dispatching_[index]) {
      continue;
    }
    if (next_due_utc == 0 || task.next_run_utc < next_due_utc) {
      next_due_utc = task.next_run_utc;
    }
  }

  xSemaphoreGive(mutex_);
  return next_due_utc;
}

int ScheduledSms::FindTaskIndexLocked(uint32_t task_id) const {
  for (size_t index = 0; index < task_count_; ++index) {
    if (tasks_[index].id == task_id) {
      return static_cast<int>(index);
    }
  }
  return -1;
}

bool ScheduledSms::ValidateDraftLocked(const ScheduledTaskDraft& draft,
                                       String& message) const {
  if (draft.phone.length() == 0) {
    message = "请输入目标号码。";
    return false;
  }
  if (draft.body.length() == 0) {
    message = "请输入短信内容。";
    return false;
  }
  if (draft.body.length() > kMaxOutboundSmsUtf8Bytes) {
    message = "短信内容超过当前支持上限。";
    return false;
  }
  const OutboundSmsPolicyStatus sms_policy =
      AnalyzeOutboundSms(draft.phone.c_str(), draft.body);
  if (sms_policy == OutboundSmsPolicyStatus::TooManyParts) {
    message = "短信内容超过 5 段发送上限。";
    return false;
  }
  if (sms_policy != OutboundSmsPolicyStatus::CanSend) {
    message = "短信内容无法按当前编码规则发送。";
    return false;
  }
  if (draft.first_run_utc <= 0) {
    message = "请选择首次发送时间。";
    return false;
  }
  if (draft.repeat_enabled && draft.repeat_every == 0) {
    message = "重复发送间隔必须大于 0。";
    return false;
  }
  if (draft.end_policy == ScheduledEndPolicy::OnDate && draft.end_at_utc <= 0) {
    message = "请选择结束时间。";
    return false;
  }
  if (draft.end_policy == ScheduledEndPolicy::AfterRuns && draft.max_runs == 0) {
    message = "请输入发送次数上限。";
    return false;
  }
  return true;
}

ScheduledTaskRecord ScheduledSms::BuildRecordLocked(
    const ScheduledTaskDraft& draft, const ScheduledTaskRecord* existing) const {
  ScheduledTaskRecord record = existing != nullptr ? *existing : ScheduledTaskRecord{};
  const time_t now_utc = time(nullptr);

  record.id = existing != nullptr ? existing->id : next_task_id_;
  record.enabled = draft.enabled;
  record.name = draft.name;
  record.phone = draft.phone;
  record.preview = BuildPreviewLocked(draft.body);
  record.body_bytes = static_cast<size_t>(draft.body.length());
  record.first_run_utc = draft.first_run_utc;
  record.repeat_enabled = draft.repeat_enabled;
  record.repeat_every = draft.repeat_enabled ? draft.repeat_every : 0;
  record.repeat_unit = draft.repeat_unit;
  record.end_policy = draft.repeat_enabled ? draft.end_policy : ScheduledEndPolicy::Never;
  record.end_at_utc =
      record.end_policy == ScheduledEndPolicy::OnDate ? draft.end_at_utc : 0;
  record.max_runs =
      record.end_policy == ScheduledEndPolicy::AfterRuns ? draft.max_runs : 0;

  if (existing == nullptr) {
    record.run_count = 0;
    record.last_run_utc = 0;
    record.last_run_success = false;
    record.last_result = "";
  }

  if (!record.enabled) {
    record.next_run_utc = 0;
  } else {
    record.next_run_utc = ComputeInitialNextRunUtcLocked(draft, now_utc);
  }
  return record;
}

time_t ScheduledSms::ComputeInitialNextRunUtcLocked(const ScheduledTaskDraft& draft,
                                                    time_t now_utc) const {
  if (!draft.repeat_enabled) {
    return draft.first_run_utc;
  }

  if (!IsClockValid(now_utc)) {
    return draft.first_run_utc;
  }

  time_t candidate = draft.first_run_utc;
  while (candidate != 0 && candidate <= now_utc) {
    candidate = AddIntervalUtc(candidate, draft.repeat_every, draft.repeat_unit);
    if (draft.end_policy == ScheduledEndPolicy::OnDate && draft.end_at_utc > 0 &&
        candidate > draft.end_at_utc) {
      return 0;
    }
  }
  return candidate;
}

time_t ScheduledSms::ComputeNextRunAfterLocked(const ScheduledTaskRecord& task,
                                               time_t after_utc) const {
  if (!task.repeat_enabled || task.repeat_every == 0) {
    return 0;
  }

  time_t candidate = task.first_run_utc;
  while (candidate != 0 && candidate <= after_utc) {
    candidate = AddIntervalUtc(candidate, task.repeat_every, task.repeat_unit);
    if (HasReachedEndLocked(task, candidate)) {
      return 0;
    }
  }
  return candidate;
}

bool ScheduledSms::HasReachedEndLocked(const ScheduledTaskRecord& task,
                                       time_t candidate_utc) const {
  if (candidate_utc == 0) {
    return true;
  }
  if (task.end_policy == ScheduledEndPolicy::OnDate && task.end_at_utc > 0 &&
      candidate_utc > task.end_at_utc) {
    return true;
  }
  if (task.end_policy == ScheduledEndPolicy::AfterRuns && task.max_runs > 0 &&
      task.run_count >= task.max_runs) {
    return true;
  }
  return false;
}

String ScheduledSms::BuildPreviewLocked(const String& body) const {
  String preview;
  preview.reserve(kScheduledTaskPreviewMaxBytes);

  for (size_t index = 0; index < static_cast<size_t>(body.length()); ++index) {
    const char ch = body[static_cast<int>(index)];
    if (ch == '\r' || ch == '\n') {
      if (!preview.endsWith(" ")) {
        preview += ' ';
      }
      continue;
    }
    preview += ch;
    if (preview.length() >= kScheduledTaskPreviewMaxBytes) {
      break;
    }
  }

  if (body.length() > preview.length()) {
    preview += "...";
  }
  return preview;
}

void ScheduledSms::SortTasksLocked() {
  for (size_t outer = 0; outer < task_count_; ++outer) {
    for (size_t inner = outer + 1; inner < task_count_; ++inner) {
      if (tasks_[inner].id >= tasks_[outer].id) {
        continue;
      }

      const ScheduledTaskRecord record = tasks_[outer];
      tasks_[outer] = tasks_[inner];
      tasks_[inner] = record;

      const bool dispatching = dispatching_[outer];
      dispatching_[outer] = dispatching_[inner];
      dispatching_[inner] = dispatching;
    }
  }
}

void ScheduledSms::FinishRunLocked(ScheduledTaskRecord& task, bool count_run,
                                   time_t executed_at, bool success,
                                   const String& result_message) {
  task.last_run_utc = executed_at;
  task.last_run_success = success;
  task.last_result = result_message;

  if (!count_run) {
    return;
  }

  task.run_count += 1;
  if (!task.repeat_enabled) {
    task.enabled = false;
    task.next_run_utc = 0;
    return;
  }
  if (task.end_policy == ScheduledEndPolicy::AfterRuns && task.max_runs > 0 &&
      task.run_count >= task.max_runs) {
    task.enabled = false;
    task.next_run_utc = 0;
    return;
  }

  task.next_run_utc = ComputeNextRunAfterLocked(task, executed_at);
  if (task.next_run_utc == 0) {
    task.enabled = false;
  }
}
