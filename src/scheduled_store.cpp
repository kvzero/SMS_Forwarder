/**
 * @file scheduled_store.cpp
 * @brief Scheduled SMS persistence owner backed by SPIFFS.
 */

#include "scheduled_store.h"

#include <FS.h>
#include <SPIFFS.h>

namespace {

constexpr const char* kMetadataPrefix = "/sched_meta_";
constexpr const char* kBodyPrefix = "/sched_body_";
constexpr const char* kMetadataSuffix = ".txt";
constexpr const char* kBodySuffix = ".txt";

String BuildTaskPath(const char* prefix, uint32_t task_id, const char* suffix) {
  String path(prefix);
  path += String(task_id);
  path += suffix;
  return path;
}

bool HasTaskPrefix(const String& path, const char* prefix, const char* suffix) {
  return path.startsWith(prefix) && path.endsWith(suffix);
}

}  // namespace

ScheduledStore::ScheduledStore() : mutex_(xSemaphoreCreateMutex()), mounted_(false) {}

bool ScheduledStore::Load(ScheduledTaskRecord* tasks, size_t capacity, size_t& task_count,
                          uint32_t& next_task_id, String& message) {
  task_count = 0;
  next_task_id = 1;

  if (tasks == nullptr || capacity == 0) {
    message = "定时任务存储目标无效。";
    return false;
  }

  if (!EnsureMounted(message)) {
    return false;
  }

  if (xSemaphoreTake(mutex_, portMAX_DELAY) != pdTRUE) {
    message = "定时任务存储忙，请稍后重试。";
    return false;
  }

  File root = SPIFFS.open("/");
  if (!root || !root.isDirectory()) {
    xSemaphoreGive(mutex_);
    message = "无法打开定时任务目录。";
    return false;
  }

  uint32_t max_id = 0;
  File file = root.openNextFile();
  while (file) {
    const String path = String(file.name());
    file.close();

    if (HasTaskPrefix(path, kMetadataPrefix, kMetadataSuffix)) {
      if (task_count >= capacity) {
        xSemaphoreGive(mutex_);
        message = "定时任务数量超过上限。";
        return false;
      }

      String content;
      String read_message;
      if (!ReadFileText(path, content, read_message)) {
        xSemaphoreGive(mutex_);
        message = read_message;
        return false;
      }

      ScheduledTaskRecord task;
      String parse_message;
      if (!ParseMetadata(content, task, parse_message)) {
        xSemaphoreGive(mutex_);
        message = parse_message;
        return false;
      }

      tasks[task_count++] = task;
      if (task.id > max_id) {
        max_id = task.id;
      }
    }

    file = root.openNextFile();
  }

  xSemaphoreGive(mutex_);
  next_task_id = max_id + 1;
  message = "";
  return true;
}

bool ScheduledStore::StoreTask(const ScheduledTaskRecord& task, const String& body,
                               String& message) {
  if (!EnsureMounted(message)) {
    return false;
  }

  if (xSemaphoreTake(mutex_, portMAX_DELAY) != pdTRUE) {
    message = "定时任务存储忙，请稍后重试。";
    return false;
  }

  const String body_path = BodyPath(task.id);
  const String metadata_path = MetadataPath(task.id);

  bool had_old_body = false;
  bool had_old_metadata = false;
  String old_body;
  String old_metadata;
  String read_message;

  if (SPIFFS.exists(body_path)) {
    had_old_body = ReadFileText(body_path, old_body, read_message);
    if (!had_old_body) {
      xSemaphoreGive(mutex_);
      message = read_message;
      return false;
    }
  }

  if (SPIFFS.exists(metadata_path)) {
    had_old_metadata = ReadFileText(metadata_path, old_metadata, read_message);
    if (!had_old_metadata) {
      xSemaphoreGive(mutex_);
      message = read_message;
      return false;
    }
  }

  if (!WriteFileAtomically(body_path, body, message)) {
    xSemaphoreGive(mutex_);
    return false;
  }

  const String metadata = SerializeMetadata(task);
  if (!WriteFileAtomically(metadata_path, metadata, message)) {
    String rollback_message;
    RestoreFile(body_path, old_body, had_old_body, rollback_message);
    xSemaphoreGive(mutex_);
    return false;
  }

  xSemaphoreGive(mutex_);
  message = "";
  return true;
}

bool ScheduledStore::StoreTaskRecord(const ScheduledTaskRecord& task, String& message) {
  if (!EnsureMounted(message)) {
    return false;
  }

  if (xSemaphoreTake(mutex_, portMAX_DELAY) != pdTRUE) {
    message = "定时任务存储忙，请稍后重试。";
    return false;
  }

  const String metadata_path = MetadataPath(task.id);
  if (!WriteFileAtomically(metadata_path, SerializeMetadata(task), message)) {
    xSemaphoreGive(mutex_);
    return false;
  }

  xSemaphoreGive(mutex_);
  message = "";
  return true;
}

bool ScheduledStore::DeleteTask(uint32_t task_id, String& message) {
  if (!EnsureMounted(message)) {
    return false;
  }

  if (xSemaphoreTake(mutex_, portMAX_DELAY) != pdTRUE) {
    message = "定时任务存储忙，请稍后重试。";
    return false;
  }

  const String body_path = BodyPath(task_id);
  const String metadata_path = MetadataPath(task_id);
  bool had_old_body = false;
  bool had_old_metadata = false;
  String old_body;
  String old_metadata;
  String read_message;

  if (SPIFFS.exists(body_path)) {
    had_old_body = ReadFileText(body_path, old_body, read_message);
    if (!had_old_body) {
      xSemaphoreGive(mutex_);
      message = read_message;
      return false;
    }
  }

  if (SPIFFS.exists(metadata_path)) {
    had_old_metadata = ReadFileText(metadata_path, old_metadata, read_message);
    if (!had_old_metadata) {
      xSemaphoreGive(mutex_);
      message = read_message;
      return false;
    }
  }

  if (!DeleteFileIfExists(body_path, message)) {
    xSemaphoreGive(mutex_);
    return false;
  }

  if (!DeleteFileIfExists(metadata_path, message)) {
    String rollback_message;
    RestoreFile(body_path, old_body, had_old_body, rollback_message);
    xSemaphoreGive(mutex_);
    return false;
  }

  xSemaphoreGive(mutex_);
  message = "";
  return true;
}

bool ScheduledStore::LoadTaskBody(uint32_t task_id, String& body, String& message) const {
  if (!EnsureMounted(message)) {
    return false;
  }

  if (xSemaphoreTake(mutex_, portMAX_DELAY) != pdTRUE) {
    message = "定时任务存储忙，请稍后重试。";
    return false;
  }

  const bool success = ReadFileText(BodyPath(task_id), body, message);
  xSemaphoreGive(mutex_);
  return success;
}

bool ScheduledStore::EnsureMounted(String& message) const {
  if (mounted_) {
    return true;
  }

  if (SPIFFS.begin(false)) {
    mounted_ = true;
    return true;
  }

  message = "无法挂载定时任务存储空间。";
  return false;
}

bool ScheduledStore::LoadTaskMetadata(uint32_t task_id, ScheduledTaskRecord& task,
                                      String& message) const {
  String content;
  if (!ReadFileText(MetadataPath(task_id), content, message)) {
    return false;
  }
  return ParseMetadata(content, task, message);
}

bool ScheduledStore::WriteFileAtomically(const String& path, const String& content,
                                         String& message) const {
  const String temp_path = TempPath(path);
  const String backup_path = path + ".bak";
  {
    File file = SPIFFS.open(temp_path, FILE_WRITE);
    if (!file) {
      message = "无法写入定时任务临时文件。";
      return false;
    }

    const size_t written = file.print(content);
    file.close();
    if (written != static_cast<size_t>(content.length())) {
      SPIFFS.remove(temp_path);
      message = "定时任务文件写入不完整。";
      return false;
    }
  }

  SPIFFS.remove(backup_path);
  const bool had_old_file = SPIFFS.exists(path);
  if (had_old_file && !SPIFFS.rename(path, backup_path)) {
    SPIFFS.remove(temp_path);
    message = "无法备份旧的定时任务文件。";
    return false;
  }

  if (!SPIFFS.rename(temp_path, path)) {
    if (had_old_file) {
      SPIFFS.rename(backup_path, path);
    }
    SPIFFS.remove(temp_path);
    message = "无法提交定时任务文件更新。";
    return false;
  }

  SPIFFS.remove(backup_path);
  return true;
}

bool ScheduledStore::ReadFileText(const String& path, String& content, String& message) const {
  File file = SPIFFS.open(path, FILE_READ);
  if (!file) {
    message = "无法读取定时任务文件。";
    return false;
  }
  content = file.readString();
  file.close();
  return true;
}

bool ScheduledStore::DeleteFileIfExists(const String& path, String& message) const {
  if (!SPIFFS.exists(path)) {
    return true;
  }
  if (SPIFFS.remove(path)) {
    return true;
  }
  message = "无法删除定时任务文件。";
  return false;
}

bool ScheduledStore::RestoreFile(const String& path, const String& content, bool existed,
                                 String& message) const {
  if (!existed) {
    return DeleteFileIfExists(path, message);
  }
  return WriteFileAtomically(path, content, message);
}

String ScheduledStore::MetadataPath(uint32_t task_id) const {
  return BuildTaskPath(kMetadataPrefix, task_id, kMetadataSuffix);
}

String ScheduledStore::BodyPath(uint32_t task_id) const {
  return BuildTaskPath(kBodyPrefix, task_id, kBodySuffix);
}

String ScheduledStore::TempPath(const String& path) const {
  return path + ".tmp";
}

String ScheduledStore::SerializeMetadata(const ScheduledTaskRecord& task) const {
  String content;
  content.reserve(384);
  content += "id=" + String(task.id) + "\n";
  content += "enabled=" + String(task.enabled ? 1 : 0) + "\n";
  content += "name=" + NormalizeMetadataText(task.name) + "\n";
  content += "phone=" + NormalizeMetadataText(task.phone) + "\n";
  content += "preview=" + NormalizeMetadataText(task.preview) + "\n";
  content += "body_bytes=" + String(static_cast<unsigned long>(task.body_bytes)) + "\n";
  content += "first_run_utc=" + String(static_cast<long long>(task.first_run_utc)) + "\n";
  content += "repeat_enabled=" + String(task.repeat_enabled ? 1 : 0) + "\n";
  content += "repeat_every=" + String(task.repeat_every) + "\n";
  content += "repeat_unit=" + String(static_cast<int>(task.repeat_unit)) + "\n";
  content += "end_policy=" + String(static_cast<int>(task.end_policy)) + "\n";
  content += "end_at_utc=" + String(static_cast<long long>(task.end_at_utc)) + "\n";
  content += "max_runs=" + String(task.max_runs) + "\n";
  content += "run_count=" + String(task.run_count) + "\n";
  content += "next_run_utc=" + String(static_cast<long long>(task.next_run_utc)) + "\n";
  content += "last_run_utc=" + String(static_cast<long long>(task.last_run_utc)) + "\n";
  content += "last_run_success=" + String(task.last_run_success ? 1 : 0) + "\n";
  content += "last_result=" + NormalizeMetadataText(task.last_result) + "\n";
  return content;
}

bool ScheduledStore::ParseMetadata(const String& content, ScheduledTaskRecord& task,
                                   String& message) const {
  int line_start = 0;
  while (line_start <= content.length()) {
    int line_end = content.indexOf('\n', line_start);
    if (line_end < 0) {
      line_end = content.length();
    }

    const String line = content.substring(line_start, line_end);
    if (line.length() > 0) {
      const int separator = line.indexOf('=');
      if (separator <= 0) {
        message = "定时任务元数据格式错误。";
        return false;
      }

      const String key = line.substring(0, separator);
      const String value = line.substring(separator + 1);

      if (key == "id") {
        task.id = static_cast<uint32_t>(value.toInt());
      } else if (key == "enabled") {
        task.enabled = value.toInt() != 0;
      } else if (key == "name") {
        task.name = value;
      } else if (key == "phone") {
        task.phone = value;
      } else if (key == "preview") {
        task.preview = value;
      } else if (key == "body_bytes") {
        task.body_bytes = static_cast<size_t>(value.toInt());
      } else if (key == "first_run_utc") {
        task.first_run_utc = static_cast<time_t>(value.toInt());
      } else if (key == "repeat_enabled") {
        task.repeat_enabled = value.toInt() != 0;
      } else if (key == "repeat_every") {
        task.repeat_every = static_cast<uint32_t>(value.toInt());
      } else if (key == "repeat_unit") {
        task.repeat_unit =
            static_cast<ScheduledIntervalUnit>(value.toInt());
      } else if (key == "end_policy") {
        task.end_policy = static_cast<ScheduledEndPolicy>(value.toInt());
      } else if (key == "end_at_utc") {
        task.end_at_utc = static_cast<time_t>(value.toInt());
      } else if (key == "max_runs") {
        task.max_runs = static_cast<uint32_t>(value.toInt());
      } else if (key == "run_count") {
        task.run_count = static_cast<uint32_t>(value.toInt());
      } else if (key == "next_run_utc") {
        task.next_run_utc = static_cast<time_t>(value.toInt());
      } else if (key == "last_run_utc") {
        task.last_run_utc = static_cast<time_t>(value.toInt());
      } else if (key == "last_run_success") {
        task.last_run_success = value.toInt() != 0;
      } else if (key == "last_result") {
        task.last_result = value;
      }
    }

    line_start = line_end + 1;
  }

  if (task.id == 0) {
    message = "定时任务元数据缺少任务编号。";
    return false;
  }
  return true;
}

String ScheduledStore::NormalizeMetadataText(const String& value) const {
  String normalized = value;
  normalized.replace("\r", " ");
  normalized.replace("\n", " ");
  return normalized;
}
