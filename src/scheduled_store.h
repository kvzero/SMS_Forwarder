/**
 * @file scheduled_store.h
 * @brief Scheduled SMS persistence owner backed by SPIFFS.
 */

#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "scheduled_sms.h"

class ScheduledStore {
 public:
  ScheduledStore();

  bool Load(ScheduledTaskRecord* tasks, size_t capacity, size_t& task_count,
            uint32_t& next_task_id, String& message);
  bool StoreTask(const ScheduledTaskRecord& task, const String& body, String& message);
  bool StoreTaskRecord(const ScheduledTaskRecord& task, String& message);
  bool DeleteTask(uint32_t task_id, String& message);
  bool LoadTaskBody(uint32_t task_id, String& body, String& message) const;

 private:
  bool EnsureMounted(String& message) const;
  bool LoadTaskMetadata(uint32_t task_id, ScheduledTaskRecord& task, String& message) const;
  bool WriteFileAtomically(const String& path, const String& content,
                           String& message) const;
  bool ReadFileText(const String& path, String& content, String& message) const;
  bool DeleteFileIfExists(const String& path, String& message) const;
  bool RestoreFile(const String& path, const String& content, bool existed,
                   String& message) const;
  String MetadataPath(uint32_t task_id) const;
  String BodyPath(uint32_t task_id) const;
  String TempPath(const String& path) const;
  String SerializeMetadata(const ScheduledTaskRecord& task) const;
  bool ParseMetadata(const String& content, ScheduledTaskRecord& task, String& message) const;
  String NormalizeMetadataText(const String& value) const;

  mutable SemaphoreHandle_t mutex_;
  mutable bool mounted_;
};
